// SPDX-License-Identifier: GPL-2.0-only
/* BCM4352 D11 rev42 receive-header boundary proven from pinned vendor code. */
#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/string.h>
#else
#include <errno.h>
#include <string.h>
#endif

#include "ob_rx_d11_rev42.h"

int ob_rx_d11_rev42_payload_view(const ob_rx_d11_u8 *dma_buffer,
				 ob_rx_d11_size available_length,
				 const struct ob_rx_d11_rev42_input *input,
				 struct ob_rx_d11_rev42_view *view)
{
	if (!view)
		return -EINVAL;
	view->payload = NULL;
	view->payload_length = 0;
	if (!dma_buffer || !input)
		return -EINVAL;
	if (input->d11_core_revision != 42)
		return -EOPNOTSUPP;
	if (input->length_provenance != OB_RX_D11_LENGTH_VALIDATED)
		return -ENODATA;
	if (input->complete != 1 ||
	    available_length < OB_RX_D11_REV42_PREFIX_LEN ||
	    !input->payload_length ||
	    input->payload_length > available_length -
					   OB_RX_D11_REV42_PREFIX_LEN)
		return -EMSGSIZE;

	view->payload = dma_buffer + OB_RX_D11_REV42_PREFIX_LEN;
	view->payload_length = input->payload_length;
	return 0;
}

int ob_rx_d11_rev42_decode_length(ob_rx_d11_u32 d11_core_revision,
				  const ob_rx_d11_u8 *header,
				  ob_rx_d11_size header_length,
				  ob_rx_d11_size buffer_capacity,
				  struct ob_rx_d11_rev42_length *length)
{
	ob_rx_d11_u32 rx_frame_size;
	ob_rx_d11_u32 required_dma_bytes;

	if (!length)
		return -EINVAL;
	length->rx_frame_size = 0;
	length->required_dma_bytes = 0;
	length->layout = OB_RX_D11_REV42_SINGLE_BUFFER;
	if (!header)
		return -EINVAL;
	if (d11_core_revision != 42)
		return -EOPNOTSUPP;
	if (header_length < OB_RX_D11_REV42_RXFRAMESIZE_LEN ||
	    buffer_capacity < OB_RX_D11_REV42_PREFIX_LEN)
		return -EMSGSIZE;

	/* The admitted x86 vendor object reads a 16-bit little-endian word. */
	rx_frame_size = (ob_rx_d11_u32)header[0] |
		((ob_rx_d11_u32)header[1] << 8);
	required_dma_bytes = OB_RX_D11_REV42_PREFIX_LEN + rx_frame_size;

	length->rx_frame_size = rx_frame_size;
	length->required_dma_bytes = required_dma_bytes;
	if (required_dma_bytes > buffer_capacity)
		length->layout = OB_RX_D11_REV42_NEEDS_CHAIN;
	return 0;
}

int ob_rx_d11_rev42_decode_status(ob_rx_d11_u32 d11_core_revision,
				  const ob_rx_d11_u8 *prefix,
				  ob_rx_d11_size prefix_length,
				  struct ob_rx_d11_rev42_status *status)
{
	ob_rx_d11_u32 word4;
	ob_rx_d11_u16 word16;
	ob_rx_d11_u8 byte16, byte18, skip;

	if (!status)
		return -EINVAL;
	memset(status, 0, sizeof(*status));
	if (!prefix)
		return -EINVAL;
	if (d11_core_revision != 42)
		return -EOPNOTSUPP;
	/* Highest proven access is a u16 at +0x16 and byte comparison at +0x16. */
	if (prefix_length < 0x18)
		return -EMSGSIZE;

	word4 = (ob_rx_d11_u32)prefix[4] |
		((ob_rx_d11_u32)prefix[5] << 8);
	byte16 = prefix[0x10];
	byte18 = prefix[0x12];
	word16 = (ob_rx_d11_u16)prefix[0x16] |
		((ob_rx_d11_u16)prefix[0x17] << 8);
	skip = (byte16 & 0x04u) ? 2u : 0u;

	status->raw_word_4 = word4;
	status->raw_word_0x16 = word16;
	status->raw_byte_16 = byte16;
	status->raw_byte_18 = byte18;
	status->drop_word4_branch = (word4 & 0x0310u) != 0;
	status->skip_two_bytes = skip;
	status->drop_byte16_branch = (byte16 & 0x01u) != 0;
	status->aggregate_branch = byte18 & 0x01u;
	status->raw_word_0x16_top2_selector = (ob_rx_d11_u8)(word16 >> 14);
	status->raw_byte_0x16_eq_0e = prefix[0x16] == 0x0eu;
	status->conditional_later_packet_pull_len = 6;
	status->offset_after_conditional_pull_on_continuing_path =
		(ob_rx_d11_u8)(OB_RX_D11_REV42_PREFIX_LEN + skip + 6u);
	return 0;
}

int ob_rx_d11_rev42_plan_chain(ob_rx_d11_u32 d11_core_revision,
			       ob_rx_d11_size rx_frame_size,
			       ob_rx_d11_size rx_offset,
			       ob_rx_d11_size chunk_capacity,
			       struct ob_rx_d11_rev42_chain_plan *plan)
{
	ob_rx_d11_size total, remaining, count = 1;

	if (!plan)
		return -EINVAL;
	plan->segment_count = 0;
	plan->first_segment_length = 0;
	plan->continuation_bytes = 0;
	if (d11_core_revision != 42)
		return -EOPNOTSUPP;
	if (!rx_frame_size || !chunk_capacity || rx_offset > chunk_capacity ||
	    rx_offset > (ob_rx_d11_size)-1 - rx_frame_size)
		return -EINVAL;

	total = rx_offset + rx_frame_size;
	plan->first_segment_length = total < chunk_capacity ? total :
		chunk_capacity;
	remaining = total - plan->first_segment_length;
	plan->continuation_bytes = remaining;
	while (remaining) {
		remaining -= remaining < chunk_capacity ? remaining :
			chunk_capacity;
		count++;
	}
	plan->segment_count = count;
	return 0;
}

int ob_rx_d11_rev42_copy_chain(ob_rx_d11_u32 d11_core_revision,
			       const struct ob_rx_d11_rev42_segment *segments,
			       ob_rx_d11_size segment_count,
			       ob_rx_d11_size rx_offset,
			       ob_rx_d11_size chunk_capacity,
			       ob_rx_d11_u8 *output,
			       ob_rx_d11_size output_capacity,
			       ob_rx_d11_size *output_length)
{
	struct ob_rx_d11_rev42_chain_plan plan;
	ob_rx_d11_size frame_size, copied = 0, remaining, i;
	int ret;

	if (output_length)
		*output_length = 0;
	if (!segments || !segment_count || !output || !output_length)
		return -EINVAL;
	if (d11_core_revision != 42)
		return -EOPNOTSUPP;
	if (!segments[0].data || segments[0].length < 2 ||
	    rx_offset < OB_RX_D11_REV42_RXFRAMESIZE_LEN ||
	    segments[0].length < rx_offset)
		return -EMSGSIZE;

	frame_size = (ob_rx_d11_size)segments[0].data[0] |
		((ob_rx_d11_size)segments[0].data[1] << 8);
	ret = ob_rx_d11_rev42_plan_chain(d11_core_revision, frame_size,
					 rx_offset, chunk_capacity, &plan);
	if (ret)
		return ret;
	if (segment_count != plan.segment_count ||
	    output_capacity < frame_size)
		return -EMSGSIZE;

	/* Validate all pieces before copying, so incomplete chains publish nothing. */
	if (segments[0].length < plan.first_segment_length ||
	    segments[0].length > chunk_capacity)
		return -EMSGSIZE;
	remaining = frame_size;
	if (remaining > plan.first_segment_length - rx_offset)
		remaining -= plan.first_segment_length - rx_offset;
	else
		remaining = 0;
	for (i = 1; i < segment_count; i++) {
		ob_rx_d11_size need = remaining < chunk_capacity ? remaining :
			chunk_capacity;

		if (!segments[i].data || segments[i].length < need ||
		    segments[i].length > chunk_capacity)
			return -EMSGSIZE;
		remaining -= need;
	}
	if (remaining)
		return -EMSGSIZE;

	remaining = frame_size;
	{
		ob_rx_d11_size first = plan.first_segment_length - rx_offset;

		if (first > remaining)
			first = remaining;
		memcpy(output, segments[0].data + rx_offset, first);
		copied = first;
		remaining -= first;
	}
	for (i = 1; i < segment_count && remaining; i++) {
		ob_rx_d11_size take = remaining < chunk_capacity ? remaining :
			chunk_capacity;

		memcpy(output + copied, segments[i].data, take);
		copied += take;
		remaining -= take;
	}
	if (remaining || copied != frame_size)
		return -EMSGSIZE;
	*output_length = copied;
	return 0;
}
