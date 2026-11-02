/* SPDX-License-Identifier: GPL-2.0-only */
/* Generated from the immutable A6B0F event vector plus additive overlay. */
#ifndef _OB_PHY_A6B0F_VECTOR_H_
#define _OB_PHY_A6B0F_VECTOR_H_

#include "ob_phy_a6b0f.h"

#define OB_PHY_A6B0F_SOURCE_VECTOR_SHA256 "a64519eee205cecebf4f7aecc18a444b80b4cd21bbb17a864a55e137ef913c84"
#define OB_PHY_A6B0F_OVERLAY_SHA256 "baaf270326064c0e8805168d1c4d49dace4b6e11f79594e62a5c5d50729c77f7"
static const struct ob_phy_a6b0f_event ob_phy_a6b0f_events[] = {
	{1, 3863, OB_PHY_A6B0F_PHY_READ, 0xa6b3cu, 0x019eu, 0x0000u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 255u, 0u}, /* sub_a6b0f/helper @ 0xa6b3c */
	{2, 3864, OB_PHY_A6B0F_PHY_RMW, 0xa6b5bu, 0x019eu, 0x0000u, 16u, 0x0002u, 0x0002u, OB_PHY_A6B0F_EXPR_PHY_SET_BIT, 0u, 19u, 255u, 255u, 0u}, /* sub_a6b0f/helper @ 0xa6b5b */
	{3, 3865, OB_PHY_A6B0F_PHY_READ, 0x9d991u, 0x019eu, 0x0000u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 29u, 255u, 255u, 0u}, /* sub_a6b0f/helper @ 0x9d991 */
	{4, 3866, OB_PHY_A6B0F_PHY_RMW, 0x9d9acu, 0x019eu, 0x0000u, 16u, 0x0002u, 0x0002u, OB_PHY_A6B0F_EXPR_PHY_SET_BIT, 0u, 29u, 255u, 255u, 0u}, /* sub_a6b0f/helper @ 0x9d9ac */
	{5, 3867, OB_PHY_A6B0F_TABLE_READ, 0x9da50u, 0x0007u, 0x0142u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 30u, 0u, 0u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da50 */
	{6, 3868, OB_PHY_A6B0F_TABLE_READ, 0x9da78u, 0x0007u, 0x0362u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 30u, 0u, 0u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da78 */
	{7, 3869, OB_PHY_A6B0F_TABLE_WRITE, 0x9db03u, 0x0007u, 0x0142u, 16u, 0x0000u, 0x01ffu, OB_PHY_A6B0F_EXPR_TABLE_PAIR_LOW_MASK, 5u, 30u, 0u, 0u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db03 */
	{8, 3870, OB_PHY_A6B0F_TABLE_WRITE, 0x9db22u, 0x0007u, 0x0362u, 16u, 0x0000u, 0x0101u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 30u, 0u, 0u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db22 */
	{9, 3871, OB_PHY_A6B0F_TABLE_READ, 0x9da50u, 0x0007u, 0x0143u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 31u, 0u, 1u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da50 */
	{10, 3872, OB_PHY_A6B0F_TABLE_READ, 0x9da78u, 0x0007u, 0x0363u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 31u, 0u, 1u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da78 */
	{11, 3873, OB_PHY_A6B0F_TABLE_WRITE, 0x9db03u, 0x0007u, 0x0143u, 16u, 0x0000u, 0x01ffu, OB_PHY_A6B0F_EXPR_TABLE_PAIR_LOW_MASK, 9u, 31u, 0u, 1u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db03 */
	{12, 3874, OB_PHY_A6B0F_TABLE_WRITE, 0x9db22u, 0x0007u, 0x0363u, 16u, 0x0000u, 0x0101u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 31u, 0u, 1u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db22 */
	{13, 3875, OB_PHY_A6B0F_TABLE_READ, 0x9da50u, 0x0007u, 0x0144u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 32u, 0u, 2u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da50 */
	{14, 3876, OB_PHY_A6B0F_TABLE_READ, 0x9da78u, 0x0007u, 0x0364u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 32u, 0u, 2u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da78 */
	{15, 3877, OB_PHY_A6B0F_TABLE_WRITE, 0x9db03u, 0x0007u, 0x0144u, 16u, 0x0000u, 0x01ffu, OB_PHY_A6B0F_EXPR_TABLE_PAIR_LOW_MASK, 13u, 32u, 0u, 2u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db03 */
	{16, 3878, OB_PHY_A6B0F_TABLE_WRITE, 0x9db22u, 0x0007u, 0x0364u, 16u, 0x0000u, 0x0101u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 32u, 0u, 2u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db22 */
	{17, 3879, OB_PHY_A6B0F_TABLE_READ, 0x9da50u, 0x0007u, 0x0145u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 33u, 0u, 3u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da50 */
	{18, 3880, OB_PHY_A6B0F_TABLE_READ, 0x9da78u, 0x0007u, 0x0365u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 33u, 0u, 3u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da78 */
	{19, 3881, OB_PHY_A6B0F_TABLE_WRITE, 0x9db03u, 0x0007u, 0x0145u, 16u, 0x0000u, 0x01ffu, OB_PHY_A6B0F_EXPR_TABLE_PAIR_LOW_MASK, 17u, 33u, 0u, 3u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db03 */
	{20, 3882, OB_PHY_A6B0F_TABLE_WRITE, 0x9db22u, 0x0007u, 0x0365u, 16u, 0x0000u, 0x0101u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 33u, 0u, 3u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db22 */
	{21, 3883, OB_PHY_A6B0F_TABLE_READ, 0x9da50u, 0x0007u, 0x0146u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 34u, 0u, 4u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da50 */
	{22, 3884, OB_PHY_A6B0F_TABLE_READ, 0x9da78u, 0x0007u, 0x0366u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 34u, 0u, 4u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da78 */
	{23, 3885, OB_PHY_A6B0F_TABLE_WRITE, 0x9db03u, 0x0007u, 0x0146u, 16u, 0x0000u, 0x01ffu, OB_PHY_A6B0F_EXPR_TABLE_PAIR_LOW_MASK, 21u, 34u, 0u, 4u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db03 */
	{24, 3886, OB_PHY_A6B0F_TABLE_WRITE, 0x9db22u, 0x0007u, 0x0366u, 16u, 0x0000u, 0x0101u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 34u, 0u, 4u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db22 */
	{25, 3887, OB_PHY_A6B0F_TABLE_READ, 0x9da50u, 0x0007u, 0x0147u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 35u, 0u, 5u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da50 */
	{26, 3888, OB_PHY_A6B0F_TABLE_READ, 0x9da78u, 0x0007u, 0x0367u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 35u, 0u, 5u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da78 */
	{27, 3889, OB_PHY_A6B0F_TABLE_WRITE, 0x9db03u, 0x0007u, 0x0147u, 16u, 0x0000u, 0x01ffu, OB_PHY_A6B0F_EXPR_TABLE_PAIR_LOW_MASK, 25u, 35u, 0u, 5u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db03 */
	{28, 3890, OB_PHY_A6B0F_TABLE_WRITE, 0x9db22u, 0x0007u, 0x0367u, 16u, 0x0000u, 0x0101u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 35u, 0u, 5u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db22 */
	{29, 3891, OB_PHY_A6B0F_TABLE_READ, 0x9da50u, 0x0007u, 0x0148u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 36u, 0u, 6u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da50 */
	{30, 3892, OB_PHY_A6B0F_TABLE_READ, 0x9da78u, 0x0007u, 0x0368u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 36u, 0u, 6u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da78 */
	{31, 3893, OB_PHY_A6B0F_TABLE_WRITE, 0x9db03u, 0x0007u, 0x0148u, 16u, 0x0000u, 0x01ffu, OB_PHY_A6B0F_EXPR_TABLE_PAIR_LOW_MASK, 29u, 36u, 0u, 6u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db03 */
	{32, 3894, OB_PHY_A6B0F_TABLE_WRITE, 0x9db22u, 0x0007u, 0x0368u, 16u, 0x0000u, 0x0101u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 36u, 0u, 6u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db22 */
	{33, 3895, OB_PHY_A6B0F_TABLE_READ, 0x9da50u, 0x0007u, 0x0149u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 37u, 0u, 7u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da50 */
	{34, 3896, OB_PHY_A6B0F_TABLE_READ, 0x9da78u, 0x0007u, 0x0369u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 37u, 0u, 7u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da78 */
	{35, 3897, OB_PHY_A6B0F_TABLE_WRITE, 0x9db03u, 0x0007u, 0x0149u, 16u, 0x0000u, 0x01ffu, OB_PHY_A6B0F_EXPR_TABLE_PAIR_LOW_MASK, 33u, 37u, 0u, 7u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db03 */
	{36, 3898, OB_PHY_A6B0F_TABLE_WRITE, 0x9db22u, 0x0007u, 0x0369u, 16u, 0x0000u, 0x0101u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 37u, 0u, 7u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db22 */
	{37, 3899, OB_PHY_A6B0F_TABLE_READ, 0x9da50u, 0x0007u, 0x014au, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 38u, 0u, 8u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da50 */
	{38, 3900, OB_PHY_A6B0F_TABLE_READ, 0x9da78u, 0x0007u, 0x036au, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 38u, 0u, 8u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da78 */
	{39, 3901, OB_PHY_A6B0F_TABLE_WRITE, 0x9db03u, 0x0007u, 0x014au, 16u, 0x0000u, 0x01ffu, OB_PHY_A6B0F_EXPR_TABLE_PAIR_LOW_MASK, 37u, 38u, 0u, 8u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db03 */
	{40, 3902, OB_PHY_A6B0F_TABLE_WRITE, 0x9db22u, 0x0007u, 0x036au, 16u, 0x0000u, 0x0101u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 38u, 0u, 8u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db22 */
	{41, 3903, OB_PHY_A6B0F_TABLE_READ, 0x9da50u, 0x0007u, 0x0152u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 39u, 1u, 0u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da50 */
	{42, 3904, OB_PHY_A6B0F_TABLE_READ, 0x9da78u, 0x0007u, 0x0372u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 39u, 1u, 0u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da78 */
	{43, 3905, OB_PHY_A6B0F_TABLE_WRITE, 0x9db03u, 0x0007u, 0x0152u, 16u, 0x0000u, 0x01ffu, OB_PHY_A6B0F_EXPR_TABLE_PAIR_LOW_MASK, 41u, 39u, 1u, 0u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db03 */
	{44, 3906, OB_PHY_A6B0F_TABLE_WRITE, 0x9db22u, 0x0007u, 0x0372u, 16u, 0x0000u, 0x0101u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 39u, 1u, 0u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db22 */
	{45, 3907, OB_PHY_A6B0F_TABLE_READ, 0x9da50u, 0x0007u, 0x0153u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 40u, 1u, 1u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da50 */
	{46, 3908, OB_PHY_A6B0F_TABLE_READ, 0x9da78u, 0x0007u, 0x0373u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 40u, 1u, 1u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da78 */
	{47, 3909, OB_PHY_A6B0F_TABLE_WRITE, 0x9db03u, 0x0007u, 0x0153u, 16u, 0x0000u, 0x01ffu, OB_PHY_A6B0F_EXPR_TABLE_PAIR_LOW_MASK, 45u, 40u, 1u, 1u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db03 */
	{48, 3910, OB_PHY_A6B0F_TABLE_WRITE, 0x9db22u, 0x0007u, 0x0373u, 16u, 0x0000u, 0x0101u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 40u, 1u, 1u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db22 */
	{49, 3911, OB_PHY_A6B0F_TABLE_READ, 0x9da50u, 0x0007u, 0x0154u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 41u, 1u, 2u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da50 */
	{50, 3912, OB_PHY_A6B0F_TABLE_READ, 0x9da78u, 0x0007u, 0x0374u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 41u, 1u, 2u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da78 */
	{51, 3913, OB_PHY_A6B0F_TABLE_WRITE, 0x9db03u, 0x0007u, 0x0154u, 16u, 0x0000u, 0x01ffu, OB_PHY_A6B0F_EXPR_TABLE_PAIR_LOW_MASK, 49u, 41u, 1u, 2u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db03 */
	{52, 3914, OB_PHY_A6B0F_TABLE_WRITE, 0x9db22u, 0x0007u, 0x0374u, 16u, 0x0000u, 0x0101u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 41u, 1u, 2u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db22 */
	{53, 3915, OB_PHY_A6B0F_TABLE_READ, 0x9da50u, 0x0007u, 0x0155u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 42u, 1u, 3u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da50 */
	{54, 3916, OB_PHY_A6B0F_TABLE_READ, 0x9da78u, 0x0007u, 0x0375u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 42u, 1u, 3u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da78 */
	{55, 3917, OB_PHY_A6B0F_TABLE_WRITE, 0x9db03u, 0x0007u, 0x0155u, 16u, 0x0000u, 0x01ffu, OB_PHY_A6B0F_EXPR_TABLE_PAIR_LOW_MASK, 53u, 42u, 1u, 3u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db03 */
	{56, 3918, OB_PHY_A6B0F_TABLE_WRITE, 0x9db22u, 0x0007u, 0x0375u, 16u, 0x0000u, 0x0101u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 42u, 1u, 3u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db22 */
	{57, 3919, OB_PHY_A6B0F_TABLE_READ, 0x9da50u, 0x0007u, 0x0156u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 43u, 1u, 4u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da50 */
	{58, 3920, OB_PHY_A6B0F_TABLE_READ, 0x9da78u, 0x0007u, 0x0376u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 43u, 1u, 4u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da78 */
	{59, 3921, OB_PHY_A6B0F_TABLE_WRITE, 0x9db03u, 0x0007u, 0x0156u, 16u, 0x0000u, 0x01ffu, OB_PHY_A6B0F_EXPR_TABLE_PAIR_LOW_MASK, 57u, 43u, 1u, 4u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db03 */
	{60, 3922, OB_PHY_A6B0F_TABLE_WRITE, 0x9db22u, 0x0007u, 0x0376u, 16u, 0x0000u, 0x0101u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 43u, 1u, 4u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db22 */
	{61, 3923, OB_PHY_A6B0F_TABLE_READ, 0x9da50u, 0x0007u, 0x0157u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 44u, 1u, 5u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da50 */
	{62, 3924, OB_PHY_A6B0F_TABLE_READ, 0x9da78u, 0x0007u, 0x0377u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 44u, 1u, 5u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da78 */
	{63, 3925, OB_PHY_A6B0F_TABLE_WRITE, 0x9db03u, 0x0007u, 0x0157u, 16u, 0x0000u, 0x01ffu, OB_PHY_A6B0F_EXPR_TABLE_PAIR_LOW_MASK, 61u, 44u, 1u, 5u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db03 */
	{64, 3926, OB_PHY_A6B0F_TABLE_WRITE, 0x9db22u, 0x0007u, 0x0377u, 16u, 0x0000u, 0x0101u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 44u, 1u, 5u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db22 */
	{65, 3927, OB_PHY_A6B0F_TABLE_READ, 0x9da50u, 0x0007u, 0x0158u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 45u, 1u, 6u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da50 */
	{66, 3928, OB_PHY_A6B0F_TABLE_READ, 0x9da78u, 0x0007u, 0x0378u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 45u, 1u, 6u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da78 */
	{67, 3929, OB_PHY_A6B0F_TABLE_WRITE, 0x9db03u, 0x0007u, 0x0158u, 16u, 0x0000u, 0x01ffu, OB_PHY_A6B0F_EXPR_TABLE_PAIR_LOW_MASK, 65u, 45u, 1u, 6u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db03 */
	{68, 3930, OB_PHY_A6B0F_TABLE_WRITE, 0x9db22u, 0x0007u, 0x0378u, 16u, 0x0000u, 0x0101u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 45u, 1u, 6u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db22 */
	{69, 3931, OB_PHY_A6B0F_TABLE_READ, 0x9da50u, 0x0007u, 0x0159u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 46u, 1u, 7u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da50 */
	{70, 3932, OB_PHY_A6B0F_TABLE_READ, 0x9da78u, 0x0007u, 0x0379u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 46u, 1u, 7u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da78 */
	{71, 3933, OB_PHY_A6B0F_TABLE_WRITE, 0x9db03u, 0x0007u, 0x0159u, 16u, 0x0000u, 0x01ffu, OB_PHY_A6B0F_EXPR_TABLE_PAIR_LOW_MASK, 69u, 46u, 1u, 7u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db03 */
	{72, 3934, OB_PHY_A6B0F_TABLE_WRITE, 0x9db22u, 0x0007u, 0x0379u, 16u, 0x0000u, 0x0101u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 46u, 1u, 7u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db22 */
	{73, 3935, OB_PHY_A6B0F_TABLE_READ, 0x9da50u, 0x0007u, 0x015au, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 47u, 1u, 8u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da50 */
	{74, 3936, OB_PHY_A6B0F_TABLE_READ, 0x9da78u, 0x0007u, 0x037au, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 47u, 1u, 8u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9da78 */
	{75, 3937, OB_PHY_A6B0F_TABLE_WRITE, 0x9db03u, 0x0007u, 0x015au, 16u, 0x0000u, 0x01ffu, OB_PHY_A6B0F_EXPR_TABLE_PAIR_LOW_MASK, 73u, 47u, 1u, 8u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db03 */
	{76, 3938, OB_PHY_A6B0F_TABLE_WRITE, 0x9db22u, 0x0007u, 0x037au, 16u, 0x0000u, 0x0101u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 47u, 1u, 8u, 0u}, /* wlc_phy_set_analog_tx_lpf @ 0x9db22 */
	{77, 3939, OB_PHY_A6B0F_PHY_RMW, 0x9db59u, 0x019eu, 0x0000u, 16u, 0x0002u, 0x0002u, OB_PHY_A6B0F_EXPR_PHY_RESTORE_BIT, 3u, 29u, 255u, 255u, 0u}, /* sub_a6b0f/helper @ 0x9db59 */
	{78, 3940, OB_PHY_A6B0F_PHY_READ, 0x9d4dau, 0x019eu, 0x0000u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 0u, 255u, 255u, 0u}, /* sub_a6b0f/helper @ 0x9d4da */
	{79, 3941, OB_PHY_A6B0F_PHY_RMW, 0x9d4f5u, 0x019eu, 0x0000u, 16u, 0x0002u, 0x0002u, OB_PHY_A6B0F_EXPR_PHY_SET_BIT, 0u, 0u, 255u, 255u, 0u}, /* sub_a6b0f/helper @ 0x9d4f5 */
	{80, 3942, OB_PHY_A6B0F_TABLE_READ, 0x9d591u, 0x0007u, 0x03fbu, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 1u, 0u, 0u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d591 */
	{81, 3943, OB_PHY_A6B0F_TABLE_WRITE, 0x9d60du, 0x0007u, 0x03fbu, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_CAP_LOW, 80u, 1u, 0u, 0u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d60d */
	{82, 3944, OB_PHY_A6B0F_TABLE_READ, 0x9d591u, 0x0007u, 0x03fbu, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 2u, 0u, 1u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d591 */
	{83, 3945, OB_PHY_A6B0F_TABLE_WRITE, 0x9d60du, 0x0007u, 0x03fbu, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_CAP_HIGH, 82u, 2u, 0u, 1u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d60d */
	{84, 3946, OB_PHY_A6B0F_TABLE_READ, 0x9d591u, 0x0007u, 0x03fcu, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 3u, 0u, 2u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d591 */
	{85, 3947, OB_PHY_A6B0F_TABLE_WRITE, 0x9d60du, 0x0007u, 0x03fcu, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_CAP_LOW, 84u, 3u, 0u, 2u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d60d */
	{86, 3948, OB_PHY_A6B0F_TABLE_READ, 0x9d591u, 0x0007u, 0x03fcu, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 4u, 0u, 3u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d591 */
	{87, 3949, OB_PHY_A6B0F_TABLE_WRITE, 0x9d60du, 0x0007u, 0x03fcu, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_CAP_HIGH, 86u, 4u, 0u, 3u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d60d */
	{88, 3950, OB_PHY_A6B0F_TABLE_READ, 0x9d591u, 0x0007u, 0x03feu, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 5u, 0u, 4u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d591 */
	{89, 3951, OB_PHY_A6B0F_TABLE_WRITE, 0x9d60du, 0x0007u, 0x03feu, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_CAP_LOW, 88u, 5u, 0u, 4u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d60d */
	{90, 3952, OB_PHY_A6B0F_TABLE_READ, 0x9d591u, 0x0007u, 0x03feu, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 6u, 0u, 5u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d591 */
	{91, 3953, OB_PHY_A6B0F_TABLE_WRITE, 0x9d60du, 0x0007u, 0x03feu, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_CAP_HIGH, 90u, 6u, 0u, 5u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d60d */
	{92, 3954, OB_PHY_A6B0F_TABLE_READ, 0x9d591u, 0x0007u, 0x03ffu, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 7u, 0u, 6u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d591 */
	{93, 3955, OB_PHY_A6B0F_TABLE_WRITE, 0x9d60du, 0x0007u, 0x03ffu, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_CAP_LOW, 92u, 7u, 0u, 6u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d60d */
	{94, 3956, OB_PHY_A6B0F_TABLE_READ, 0x9d591u, 0x0007u, 0x03ffu, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 8u, 0u, 7u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d591 */
	{95, 3957, OB_PHY_A6B0F_TABLE_WRITE, 0x9d60du, 0x0007u, 0x03ffu, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_CAP_HIGH, 94u, 8u, 0u, 7u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d60d */
	{96, 3958, OB_PHY_A6B0F_TABLE_READ, 0x9d591u, 0x0007u, 0x03fau, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 9u, 0u, 8u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d591 */
	{97, 3959, OB_PHY_A6B0F_TABLE_WRITE, 0x9d60du, 0x0007u, 0x03fau, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_CAP_LOW, 96u, 9u, 0u, 8u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d60d */
	{98, 3960, OB_PHY_A6B0F_TABLE_READ, 0x9d591u, 0x0007u, 0x006bu, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 10u, 1u, 0u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d591 */
	{99, 3961, OB_PHY_A6B0F_TABLE_WRITE, 0x9d60du, 0x0007u, 0x006bu, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_CAP_LOW, 98u, 10u, 1u, 0u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d60d */
	{100, 3962, OB_PHY_A6B0F_TABLE_READ, 0x9d591u, 0x0007u, 0x006bu, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 11u, 1u, 1u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d591 */
	{101, 3963, OB_PHY_A6B0F_TABLE_WRITE, 0x9d60du, 0x0007u, 0x006bu, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_CAP_HIGH, 100u, 11u, 1u, 1u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d60d */
	{102, 3964, OB_PHY_A6B0F_TABLE_READ, 0x9d591u, 0x0007u, 0x006cu, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 12u, 1u, 2u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d591 */
	{103, 3965, OB_PHY_A6B0F_TABLE_WRITE, 0x9d60du, 0x0007u, 0x006cu, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_CAP_LOW, 102u, 12u, 1u, 2u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d60d */
	{104, 3966, OB_PHY_A6B0F_TABLE_READ, 0x9d591u, 0x0007u, 0x006cu, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 13u, 1u, 3u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d591 */
	{105, 3967, OB_PHY_A6B0F_TABLE_WRITE, 0x9d60du, 0x0007u, 0x006cu, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_CAP_HIGH, 104u, 13u, 1u, 3u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d60d */
	{106, 3968, OB_PHY_A6B0F_TABLE_READ, 0x9d591u, 0x0007u, 0x006eu, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 14u, 1u, 4u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d591 */
	{107, 3969, OB_PHY_A6B0F_TABLE_WRITE, 0x9d60du, 0x0007u, 0x006eu, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_CAP_LOW, 106u, 14u, 1u, 4u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d60d */
	{108, 3970, OB_PHY_A6B0F_TABLE_READ, 0x9d591u, 0x0007u, 0x006eu, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 15u, 1u, 5u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d591 */
	{109, 3971, OB_PHY_A6B0F_TABLE_WRITE, 0x9d60du, 0x0007u, 0x006eu, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_CAP_HIGH, 108u, 15u, 1u, 5u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d60d */
	{110, 3972, OB_PHY_A6B0F_TABLE_READ, 0x9d591u, 0x0007u, 0x006fu, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 16u, 1u, 6u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d591 */
	{111, 3973, OB_PHY_A6B0F_TABLE_WRITE, 0x9d60du, 0x0007u, 0x006fu, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_CAP_LOW, 110u, 16u, 1u, 6u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d60d */
	{112, 3974, OB_PHY_A6B0F_TABLE_READ, 0x9d591u, 0x0007u, 0x006fu, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 17u, 1u, 7u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d591 */
	{113, 3975, OB_PHY_A6B0F_TABLE_WRITE, 0x9d60du, 0x0007u, 0x006fu, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_CAP_HIGH, 112u, 17u, 1u, 7u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d60d */
	{114, 3976, OB_PHY_A6B0F_TABLE_READ, 0x9d591u, 0x0007u, 0x006au, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 18u, 1u, 8u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d591 */
	{115, 3977, OB_PHY_A6B0F_TABLE_WRITE, 0x9d60du, 0x0007u, 0x006au, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_CAP_LOW, 114u, 18u, 1u, 8u, 0u}, /* wlc_phy_set_tx_afe_dacbuf_cap @ 0x9d60d */
	{116, 3978, OB_PHY_A6B0F_PHY_RMW, 0x9d64au, 0x019eu, 0x0000u, 16u, 0x0002u, 0x0002u, OB_PHY_A6B0F_EXPR_PHY_RESTORE_BIT, 78u, 0u, 255u, 255u, 0u}, /* sub_a6b0f/helper @ 0x9d64a */
	{117, 3979, OB_PHY_A6B0F_PHY_READ, 0x9d713u, 0x019eu, 0x0000u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 20u, 255u, 255u, 0u}, /* sub_a6b0f/helper @ 0x9d713 */
	{118, 3980, OB_PHY_A6B0F_PHY_RMW, 0x9d731u, 0x019eu, 0x0000u, 16u, 0x0002u, 0x0002u, OB_PHY_A6B0F_EXPR_PHY_SET_BIT, 0u, 20u, 255u, 255u, 0u}, /* sub_a6b0f/helper @ 0x9d731 */
	{119, 3981, OB_PHY_A6B0F_TABLE_READ, 0x9d807u, 0x0007u, 0x0140u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 23u, 0u, 0u, 0u}, /* wlc_phy_set_analog_rx_lpf @ 0x9d807 */
	{120, 3982, OB_PHY_A6B0F_TABLE_READ, 0x9d826u, 0x0007u, 0x0360u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 23u, 0u, 0u, 0u}, /* wlc_phy_set_analog_rx_lpf @ 0x9d826 */
	{121, 3983, OB_PHY_A6B0F_TABLE_WRITE, 0x9d8cdu, 0x0007u, 0x0140u, 16u, 0x0000u, 0xffffu, OB_PHY_A6B0F_EXPR_TABLE_PAIR_LOW_MASK, 119u, 23u, 0u, 0u, 0u}, /* wlc_phy_set_analog_rx_lpf @ 0x9d8cd */
	{122, 3984, OB_PHY_A6B0F_TABLE_WRITE, 0x9d8ecu, 0x0007u, 0x0360u, 16u, 0x0000u, 0x0100u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 23u, 0u, 0u, 0u}, /* wlc_phy_set_analog_rx_lpf @ 0x9d8ec */
	{123, 3985, OB_PHY_A6B0F_TABLE_READ, 0x9d807u, 0x0007u, 0x0150u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 24u, 1u, 0u, 0u}, /* wlc_phy_set_analog_rx_lpf @ 0x9d807 */
	{124, 3986, OB_PHY_A6B0F_TABLE_READ, 0x9d826u, 0x0007u, 0x0370u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 24u, 1u, 0u, 0u}, /* wlc_phy_set_analog_rx_lpf @ 0x9d826 */
	{125, 3987, OB_PHY_A6B0F_TABLE_WRITE, 0x9d8cdu, 0x0007u, 0x0150u, 16u, 0x0000u, 0xffffu, OB_PHY_A6B0F_EXPR_TABLE_PAIR_LOW_MASK, 123u, 24u, 1u, 0u, 0u}, /* wlc_phy_set_analog_rx_lpf @ 0x9d8cd */
	{126, 3988, OB_PHY_A6B0F_TABLE_WRITE, 0x9d8ecu, 0x0007u, 0x0370u, 16u, 0x0000u, 0x0100u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 24u, 1u, 0u, 0u}, /* wlc_phy_set_analog_rx_lpf @ 0x9d8ec */
	{127, 3989, OB_PHY_A6B0F_PHY_RMW, 0x9d926u, 0x019eu, 0x0000u, 16u, 0x0002u, 0x0002u, OB_PHY_A6B0F_EXPR_PHY_RESTORE_BIT, 117u, 20u, 255u, 255u, 0u}, /* sub_a6b0f/helper @ 0x9d926 */
	{128, 3990, OB_PHY_A6B0F_PHY_READ, 0x9d713u, 0x019eu, 0x0000u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 21u, 255u, 255u, 0u}, /* sub_a6b0f/helper @ 0x9d713 */
	{129, 3991, OB_PHY_A6B0F_PHY_RMW, 0x9d731u, 0x019eu, 0x0000u, 16u, 0x0002u, 0x0002u, OB_PHY_A6B0F_EXPR_PHY_SET_BIT, 0u, 21u, 255u, 255u, 0u}, /* sub_a6b0f/helper @ 0x9d731 */
	{130, 3992, OB_PHY_A6B0F_TABLE_READ, 0x9d807u, 0x0007u, 0x0141u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 25u, 0u, 1u, 0u}, /* wlc_phy_set_analog_rx_lpf @ 0x9d807 */
	{131, 3993, OB_PHY_A6B0F_TABLE_READ, 0x9d826u, 0x0007u, 0x0361u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 25u, 0u, 1u, 0u}, /* wlc_phy_set_analog_rx_lpf @ 0x9d826 */
	{132, 3994, OB_PHY_A6B0F_TABLE_WRITE, 0x9d8cdu, 0x0007u, 0x0141u, 16u, 0x0000u, 0xffffu, OB_PHY_A6B0F_EXPR_TABLE_PAIR_LOW_MASK, 130u, 25u, 0u, 1u, 0u}, /* wlc_phy_set_analog_rx_lpf @ 0x9d8cd */
	{133, 3995, OB_PHY_A6B0F_TABLE_WRITE, 0x9d8ecu, 0x0007u, 0x0361u, 16u, 0x0000u, 0x0100u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 25u, 0u, 1u, 0u}, /* wlc_phy_set_analog_rx_lpf @ 0x9d8ec */
	{134, 3996, OB_PHY_A6B0F_TABLE_READ, 0x9d807u, 0x0007u, 0x0151u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 26u, 1u, 1u, 0u}, /* wlc_phy_set_analog_rx_lpf @ 0x9d807 */
	{135, 3997, OB_PHY_A6B0F_TABLE_READ, 0x9d826u, 0x0007u, 0x0371u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 26u, 1u, 1u, 0u}, /* wlc_phy_set_analog_rx_lpf @ 0x9d826 */
	{136, 3998, OB_PHY_A6B0F_TABLE_WRITE, 0x9d8cdu, 0x0007u, 0x0151u, 16u, 0x0000u, 0xffffu, OB_PHY_A6B0F_EXPR_TABLE_PAIR_LOW_MASK, 134u, 26u, 1u, 1u, 0u}, /* wlc_phy_set_analog_rx_lpf @ 0x9d8cd */
	{137, 3999, OB_PHY_A6B0F_TABLE_WRITE, 0x9d8ecu, 0x0007u, 0x0371u, 16u, 0x0000u, 0x0100u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 26u, 1u, 1u, 0u}, /* wlc_phy_set_analog_rx_lpf @ 0x9d8ec */
	{138, 4000, OB_PHY_A6B0F_PHY_RMW, 0x9d926u, 0x019eu, 0x0000u, 16u, 0x0002u, 0x0002u, OB_PHY_A6B0F_EXPR_PHY_RESTORE_BIT, 128u, 21u, 255u, 255u, 0u}, /* sub_a6b0f/helper @ 0x9d926 */
	{139, 4001, OB_PHY_A6B0F_PHY_READ, 0x9d713u, 0x019eu, 0x0000u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 22u, 255u, 255u, 0u}, /* sub_a6b0f/helper @ 0x9d713 */
	{140, 4002, OB_PHY_A6B0F_PHY_RMW, 0x9d731u, 0x019eu, 0x0000u, 16u, 0x0002u, 0x0002u, OB_PHY_A6B0F_EXPR_PHY_SET_BIT, 0u, 22u, 255u, 255u, 0u}, /* sub_a6b0f/helper @ 0x9d731 */
	{141, 4003, OB_PHY_A6B0F_TABLE_READ, 0x9d807u, 0x0007u, 0x0441u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 27u, 0u, 2u, 0u}, /* wlc_phy_set_analog_rx_lpf @ 0x9d807 */
	{142, 4004, OB_PHY_A6B0F_TABLE_READ, 0x9d826u, 0x0007u, 0x0440u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 27u, 0u, 2u, 0u}, /* wlc_phy_set_analog_rx_lpf @ 0x9d826 */
	{143, 4005, OB_PHY_A6B0F_TABLE_WRITE, 0x9d8cdu, 0x0007u, 0x0441u, 16u, 0x0000u, 0xffffu, OB_PHY_A6B0F_EXPR_TABLE_PAIR_LOW_MASK, 141u, 27u, 0u, 2u, 0u}, /* wlc_phy_set_analog_rx_lpf @ 0x9d8cd */
	{144, 4006, OB_PHY_A6B0F_TABLE_WRITE, 0x9d8ecu, 0x0007u, 0x0440u, 16u, 0x0000u, 0x0100u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 27u, 0u, 2u, 0u}, /* wlc_phy_set_analog_rx_lpf @ 0x9d8ec */
	{145, 4007, OB_PHY_A6B0F_TABLE_READ, 0x9d807u, 0x0007u, 0x0443u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 28u, 1u, 2u, 0u}, /* wlc_phy_set_analog_rx_lpf @ 0x9d807 */
	{146, 4008, OB_PHY_A6B0F_TABLE_READ, 0x9d826u, 0x0007u, 0x0442u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ, 0u, 28u, 1u, 2u, 0u}, /* wlc_phy_set_analog_rx_lpf @ 0x9d826 */
	{147, 4009, OB_PHY_A6B0F_TABLE_WRITE, 0x9d8cdu, 0x0007u, 0x0443u, 16u, 0x0000u, 0xffffu, OB_PHY_A6B0F_EXPR_TABLE_PAIR_LOW_MASK, 145u, 28u, 1u, 2u, 0u}, /* wlc_phy_set_analog_rx_lpf @ 0x9d8cd */
	{148, 4010, OB_PHY_A6B0F_TABLE_WRITE, 0x9d8ecu, 0x0007u, 0x0442u, 16u, 0x0000u, 0x0100u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 28u, 1u, 2u, 0u}, /* wlc_phy_set_analog_rx_lpf @ 0x9d8ec */
	{149, 4011, OB_PHY_A6B0F_PHY_RMW, 0x9d926u, 0x019eu, 0x0000u, 16u, 0x0002u, 0x0002u, OB_PHY_A6B0F_EXPR_PHY_RESTORE_BIT, 139u, 22u, 255u, 255u, 0u}, /* sub_a6b0f/helper @ 0x9d926 */
	{150, 4012, OB_PHY_A6B0F_TABLE_WRITE, 0xa6c9eu, 0x0007u, 0x0020u, 16u, 0x0000u, 0x0004u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 0u, 16u}, /* sub_a6b0f @ 0xa6c9e */
	{151, 4013, OB_PHY_A6B0F_TABLE_WRITE, 0xa6c9eu, 0x0007u, 0x0021u, 16u, 0x0000u, 0x0003u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 1u, 0u}, /* sub_a6b0f @ 0xa6c9e */
	{152, 4014, OB_PHY_A6B0F_TABLE_WRITE, 0xa6c9eu, 0x0007u, 0x0022u, 16u, 0x0000u, 0x0006u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 2u, 0u}, /* sub_a6b0f @ 0xa6c9e */
	{153, 4015, OB_PHY_A6B0F_TABLE_WRITE, 0xa6c9eu, 0x0007u, 0x0023u, 16u, 0x0000u, 0x0005u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 3u, 0u}, /* sub_a6b0f @ 0xa6c9e */
	{154, 4016, OB_PHY_A6B0F_TABLE_WRITE, 0xa6c9eu, 0x0007u, 0x0024u, 16u, 0x0000u, 0x0002u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 4u, 0u}, /* sub_a6b0f @ 0xa6c9e */
	{155, 4017, OB_PHY_A6B0F_TABLE_WRITE, 0xa6c9eu, 0x0007u, 0x0025u, 16u, 0x0000u, 0x0001u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 5u, 0u}, /* sub_a6b0f @ 0xa6c9e */
	{156, 4018, OB_PHY_A6B0F_TABLE_WRITE, 0xa6c9eu, 0x0007u, 0x0026u, 16u, 0x0000u, 0x0008u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 6u, 0u}, /* sub_a6b0f @ 0xa6c9e */
	{157, 4019, OB_PHY_A6B0F_TABLE_WRITE, 0xa6c9eu, 0x0007u, 0x0027u, 16u, 0x0000u, 0x002au, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 7u, 0u}, /* sub_a6b0f @ 0xa6c9e */
	{158, 4020, OB_PHY_A6B0F_TABLE_WRITE, 0xa6c9eu, 0x0007u, 0x0028u, 16u, 0x0000u, 0x002bu, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 8u, 0u}, /* sub_a6b0f @ 0xa6c9e */
	{159, 4021, OB_PHY_A6B0F_TABLE_WRITE, 0xa6c9eu, 0x0007u, 0x0029u, 16u, 0x0000u, 0x000fu, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 9u, 0u}, /* sub_a6b0f @ 0xa6c9e */
	{160, 4022, OB_PHY_A6B0F_TABLE_WRITE, 0xa6c9eu, 0x0007u, 0x002au, 16u, 0x0000u, 0x001fu, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 10u, 0u}, /* sub_a6b0f @ 0xa6c9e */
	{161, 4023, OB_PHY_A6B0F_TABLE_WRITE, 0xa6c9eu, 0x0007u, 0x002bu, 16u, 0x0000u, 0x001fu, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 11u, 0u}, /* sub_a6b0f @ 0xa6c9e */
	{162, 4024, OB_PHY_A6B0F_TABLE_WRITE, 0xa6c9eu, 0x0007u, 0x002cu, 16u, 0x0000u, 0x001fu, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 12u, 0u}, /* sub_a6b0f @ 0xa6c9e */
	{163, 4025, OB_PHY_A6B0F_TABLE_WRITE, 0xa6c9eu, 0x0007u, 0x002du, 16u, 0x0000u, 0x001fu, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 13u, 0u}, /* sub_a6b0f @ 0xa6c9e */
	{164, 4026, OB_PHY_A6B0F_TABLE_WRITE, 0xa6c9eu, 0x0007u, 0x002eu, 16u, 0x0000u, 0x001fu, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 14u, 0u}, /* sub_a6b0f @ 0xa6c9e */
	{165, 4027, OB_PHY_A6B0F_TABLE_WRITE, 0xa6c9eu, 0x0007u, 0x002fu, 16u, 0x0000u, 0x001fu, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 15u, 0u}, /* sub_a6b0f @ 0xa6c9e */
	{166, 4028, OB_PHY_A6B0F_TABLE_WRITE, 0xa6cc2u, 0x0007u, 0x0090u, 16u, 0x0000u, 0x000cu, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 0u, 16u}, /* sub_a6b0f @ 0xa6cc2 */
	{167, 4029, OB_PHY_A6B0F_TABLE_WRITE, 0xa6cc2u, 0x0007u, 0x0091u, 16u, 0x0000u, 0x0002u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 1u, 0u}, /* sub_a6b0f @ 0xa6cc2 */
	{168, 4030, OB_PHY_A6B0F_TABLE_WRITE, 0xa6cc2u, 0x0007u, 0x0092u, 16u, 0x0000u, 0x0002u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 2u, 0u}, /* sub_a6b0f @ 0xa6cc2 */
	{169, 4031, OB_PHY_A6B0F_TABLE_WRITE, 0xa6cc2u, 0x0007u, 0x0093u, 16u, 0x0000u, 0x0004u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 3u, 0u}, /* sub_a6b0f @ 0xa6cc2 */
	{170, 4032, OB_PHY_A6B0F_TABLE_WRITE, 0xa6cc2u, 0x0007u, 0x0094u, 16u, 0x0000u, 0x0004u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 4u, 0u}, /* sub_a6b0f @ 0xa6cc2 */
	{171, 4033, OB_PHY_A6B0F_TABLE_WRITE, 0xa6cc2u, 0x0007u, 0x0095u, 16u, 0x0000u, 0x0006u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 5u, 0u}, /* sub_a6b0f @ 0xa6cc2 */
	{172, 4034, OB_PHY_A6B0F_TABLE_WRITE, 0xa6cc2u, 0x0007u, 0x0096u, 16u, 0x0000u, 0x0001u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 6u, 0u}, /* sub_a6b0f @ 0xa6cc2 */
	{173, 4035, OB_PHY_A6B0F_TABLE_WRITE, 0xa6cc2u, 0x0007u, 0x0097u, 16u, 0x0000u, 0x0004u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 7u, 0u}, /* sub_a6b0f @ 0xa6cc2 */
	{174, 4036, OB_PHY_A6B0F_TABLE_WRITE, 0xa6cc2u, 0x0007u, 0x0098u, 16u, 0x0000u, 0x0001u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 8u, 0u}, /* sub_a6b0f @ 0xa6cc2 */
	{175, 4037, OB_PHY_A6B0F_TABLE_WRITE, 0xa6cc2u, 0x0007u, 0x0099u, 16u, 0x0000u, 0x0002u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 9u, 0u}, /* sub_a6b0f @ 0xa6cc2 */
	{176, 4038, OB_PHY_A6B0F_TABLE_WRITE, 0xa6cc2u, 0x0007u, 0x009au, 16u, 0x0000u, 0x0001u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 10u, 0u}, /* sub_a6b0f @ 0xa6cc2 */
	{177, 4039, OB_PHY_A6B0F_TABLE_WRITE, 0xa6cc2u, 0x0007u, 0x009bu, 16u, 0x0000u, 0x0001u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 11u, 0u}, /* sub_a6b0f @ 0xa6cc2 */
	{178, 4040, OB_PHY_A6B0F_TABLE_WRITE, 0xa6cc2u, 0x0007u, 0x009cu, 16u, 0x0000u, 0x0001u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 12u, 0u}, /* sub_a6b0f @ 0xa6cc2 */
	{179, 4041, OB_PHY_A6B0F_TABLE_WRITE, 0xa6cc2u, 0x0007u, 0x009du, 16u, 0x0000u, 0x0001u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 13u, 0u}, /* sub_a6b0f @ 0xa6cc2 */
	{180, 4042, OB_PHY_A6B0F_TABLE_WRITE, 0xa6cc2u, 0x0007u, 0x009eu, 16u, 0x0000u, 0x0001u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 14u, 0u}, /* sub_a6b0f @ 0xa6cc2 */
	{181, 4043, OB_PHY_A6B0F_TABLE_WRITE, 0xa6cc2u, 0x0007u, 0x009fu, 16u, 0x0000u, 0x0001u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 15u, 0u}, /* sub_a6b0f @ 0xa6cc2 */
	{182, 4044, OB_PHY_A6B0F_TABLE_WRITE, 0xa6ce6u, 0x0007u, 0x0121u, 16u, 0x0000u, 0x0aaau, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 0u, 2u}, /* sub_a6b0f @ 0xa6ce6 */
	{183, 4045, OB_PHY_A6B0F_TABLE_WRITE, 0xa6ce6u, 0x0007u, 0x0122u, 16u, 0x0000u, 0x0aaau, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 1u, 0u}, /* sub_a6b0f @ 0xa6ce6 */
	{184, 4046, OB_PHY_A6B0F_TABLE_WRITE, 0xa6d0au, 0x0007u, 0x0131u, 16u, 0x0000u, 0x0aaau, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 0u, 2u}, /* sub_a6b0f @ 0xa6d0a */
	{185, 4047, OB_PHY_A6B0F_TABLE_WRITE, 0xa6d0au, 0x0007u, 0x0132u, 16u, 0x0000u, 0x0aaau, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 1u, 0u}, /* sub_a6b0f @ 0xa6d0a */
	{186, 4048, OB_PHY_A6B0F_TABLE_WRITE, 0xa6d2eu, 0x0007u, 0x0124u, 16u, 0x0000u, 0x0222u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 0u, 2u}, /* sub_a6b0f @ 0xa6d2e */
	{187, 4049, OB_PHY_A6B0F_TABLE_WRITE, 0xa6d2eu, 0x0007u, 0x0125u, 16u, 0x0000u, 0x0222u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 1u, 0u}, /* sub_a6b0f @ 0xa6d2e */
	{188, 4050, OB_PHY_A6B0F_TABLE_WRITE, 0xa6d52u, 0x0007u, 0x0137u, 16u, 0x0000u, 0x0222u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 0u, 2u}, /* sub_a6b0f @ 0xa6d52 */
	{189, 4051, OB_PHY_A6B0F_TABLE_WRITE, 0xa6d52u, 0x0007u, 0x0138u, 16u, 0x0000u, 0x0222u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 1u, 0u}, /* sub_a6b0f @ 0xa6d52 */
	{190, 4052, OB_PHY_A6B0F_TABLE_WRITE, 0xa6e2du, 0x0007u, 0x0000u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 0u, 16u}, /* sub_a6b0f @ 0xa6e2d */
	{191, 4053, OB_PHY_A6B0F_TABLE_WRITE, 0xa6e2du, 0x0007u, 0x0001u, 16u, 0x0000u, 0x0001u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 1u, 0u}, /* sub_a6b0f @ 0xa6e2d */
	{192, 4054, OB_PHY_A6B0F_TABLE_WRITE, 0xa6e2du, 0x0007u, 0x0002u, 16u, 0x0000u, 0x0002u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 2u, 0u}, /* sub_a6b0f @ 0xa6e2d */
	{193, 4055, OB_PHY_A6B0F_TABLE_WRITE, 0xa6e2du, 0x0007u, 0x0003u, 16u, 0x0000u, 0x0008u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 3u, 0u}, /* sub_a6b0f @ 0xa6e2d */
	{194, 4056, OB_PHY_A6B0F_TABLE_WRITE, 0xa6e2du, 0x0007u, 0x0004u, 16u, 0x0000u, 0x0005u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 4u, 0u}, /* sub_a6b0f @ 0xa6e2d */
	{195, 4057, OB_PHY_A6B0F_TABLE_WRITE, 0xa6e2du, 0x0007u, 0x0005u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 5u, 0u}, /* sub_a6b0f @ 0xa6e2d */
	{196, 4058, OB_PHY_A6B0F_TABLE_WRITE, 0xa6e2du, 0x0007u, 0x0006u, 16u, 0x0000u, 0x0006u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 6u, 0u}, /* sub_a6b0f @ 0xa6e2d */
	{197, 4059, OB_PHY_A6B0F_TABLE_WRITE, 0xa6e2du, 0x0007u, 0x0007u, 16u, 0x0000u, 0x0003u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 7u, 0u}, /* sub_a6b0f @ 0xa6e2d */
	{198, 4060, OB_PHY_A6B0F_TABLE_WRITE, 0xa6e2du, 0x0007u, 0x0008u, 16u, 0x0000u, 0x000fu, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 8u, 0u}, /* sub_a6b0f @ 0xa6e2d */
	{199, 4061, OB_PHY_A6B0F_TABLE_WRITE, 0xa6e2du, 0x0007u, 0x0009u, 16u, 0x0000u, 0x0004u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 9u, 0u}, /* sub_a6b0f @ 0xa6e2d */
	{200, 4062, OB_PHY_A6B0F_TABLE_WRITE, 0xa6e2du, 0x0007u, 0x000au, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 10u, 0u}, /* sub_a6b0f @ 0xa6e2d */
	{201, 4063, OB_PHY_A6B0F_TABLE_WRITE, 0xa6e2du, 0x0007u, 0x000bu, 16u, 0x0000u, 0x0035u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 11u, 0u}, /* sub_a6b0f @ 0xa6e2d */
	{202, 4064, OB_PHY_A6B0F_TABLE_WRITE, 0xa6e2du, 0x0007u, 0x000cu, 16u, 0x0000u, 0x000fu, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 12u, 0u}, /* sub_a6b0f @ 0xa6e2d */
	{203, 4065, OB_PHY_A6B0F_TABLE_WRITE, 0xa6e2du, 0x0007u, 0x000du, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 13u, 0u}, /* sub_a6b0f @ 0xa6e2d */
	{204, 4066, OB_PHY_A6B0F_TABLE_WRITE, 0xa6e2du, 0x0007u, 0x000eu, 16u, 0x0000u, 0x0036u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 14u, 0u}, /* sub_a6b0f @ 0xa6e2d */
	{205, 4067, OB_PHY_A6B0F_TABLE_WRITE, 0xa6e2du, 0x0007u, 0x000fu, 16u, 0x0000u, 0x001fu, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 15u, 0u}, /* sub_a6b0f @ 0xa6e2d */
	{206, 4068, OB_PHY_A6B0F_TABLE_WRITE, 0xa6e4du, 0x0007u, 0x03c6u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 0u, 0u}, /* sub_a6b0f @ 0xa6e4d */
	{207, 4069, OB_PHY_A6B0F_TABLE_WRITE, 0xa6e6du, 0x0007u, 0x03c7u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 0u, 0u}, /* sub_a6b0f @ 0xa6e6d */
	{208, 4070, OB_PHY_A6B0F_TABLE_WRITE, 0xa6e8du, 0x0007u, 0x03d6u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 0u, 0u}, /* sub_a6b0f @ 0xa6e8d */
	{209, 4071, OB_PHY_A6B0F_TABLE_WRITE, 0xa6eadu, 0x0007u, 0x03d7u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 0u, 0u}, /* sub_a6b0f @ 0xa6ead */
	{210, 4072, OB_PHY_A6B0F_TABLE_WRITE, 0xa6ecdu, 0x0007u, 0x03e6u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 0u, 0u}, /* sub_a6b0f @ 0xa6ecd */
	{211, 4073, OB_PHY_A6B0F_TABLE_WRITE, 0xa6eedu, 0x0007u, 0x03e7u, 16u, 0x0000u, 0x0000u, OB_PHY_A6B0F_EXPR_LITERAL, 0u, 19u, 255u, 0u, 0u}, /* sub_a6b0f @ 0xa6eed */
	{212, 4074, OB_PHY_A6B0F_PHY_RMW, 0xa7075u, 0x019eu, 0x0000u, 16u, 0x0002u, 0x0002u, OB_PHY_A6B0F_EXPR_PHY_RESTORE_BIT, 1u, 19u, 255u, 255u, 0u}, /* sub_a6b0f/helper @ 0xa7075 */
};

static const struct ob_phy_a6b0f_event ob_phy_a6b0f_child_events[] = {
	{3, 3865, OB_PHY_A6B0F_PHY_READ, 0xa6052u, 0x019eu, 0u, 16u, 0u, 0u, OB_PHY_A6B0F_EXPR_CHILD_SNAPSHOT, 0u, 0u, 255u, 255u, 0u},
	{4, 3866, OB_PHY_A6B0F_PHY_RMW, 0xa6070u, 0x019eu, 0u, 16u, 0x0002u, 0x0002u, OB_PHY_A6B0F_EXPR_PHY_SET_BIT, 0u, 0u, 255u, 255u, 0u},
	{5, 3867, OB_PHY_A6B0F_PHY_RMW, 0xa6af8u, 0x019eu, 0u, 16u, 0x0002u, 0x0002u, OB_PHY_A6B0F_EXPR_CHILD_RESTORE_BIT, 3u, 0u, 255u, 255u, 0u},
};

#define OB_PHY_A6B0F_VECTOR_COUNT \
	(sizeof(ob_phy_a6b0f_events) / sizeof(ob_phy_a6b0f_events[0]))
#define OB_PHY_A6B0F_CHILD_VECTOR_COUNT \
	(sizeof(ob_phy_a6b0f_child_events) / sizeof(ob_phy_a6b0f_child_events[0]))

#endif /* _OB_PHY_A6B0F_VECTOR_H_ */
