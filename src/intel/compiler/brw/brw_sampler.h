/* Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "compiler/nir/nir.h"
#include "brw_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

enum brw_sampler_payload_param {
   BRW_SAMPLER_PAYLOAD_PARAM_INVALID,

   BRW_SAMPLER_PAYLOAD_PARAM_U,
   BRW_SAMPLER_PAYLOAD_PARAM_V,
   BRW_SAMPLER_PAYLOAD_PARAM_R,
   BRW_SAMPLER_PAYLOAD_PARAM_AI,
   BRW_SAMPLER_PAYLOAD_PARAM_BIAS,
   BRW_SAMPLER_PAYLOAD_PARAM_LOD,
   BRW_SAMPLER_PAYLOAD_PARAM_MLOD,
   BRW_SAMPLER_PAYLOAD_PARAM_REF,
   BRW_SAMPLER_PAYLOAD_PARAM_DUDX,
   BRW_SAMPLER_PAYLOAD_PARAM_DUDY,
   BRW_SAMPLER_PAYLOAD_PARAM_DVDX,
   BRW_SAMPLER_PAYLOAD_PARAM_DVDY,
   BRW_SAMPLER_PAYLOAD_PARAM_DRDX,
   BRW_SAMPLER_PAYLOAD_PARAM_DRDY,
   BRW_SAMPLER_PAYLOAD_PARAM_OFFU,
   BRW_SAMPLER_PAYLOAD_PARAM_OFFV,
   BRW_SAMPLER_PAYLOAD_PARAM_OFFUV4,
   BRW_SAMPLER_PAYLOAD_PARAM_OFFUVR4,
   BRW_SAMPLER_PAYLOAD_PARAM_OFFUV6,
   BRW_SAMPLER_PAYLOAD_PARAM_OFFUVR6,
   BRW_SAMPLER_PAYLOAD_PARAM_BIAS_AI,
   BRW_SAMPLER_PAYLOAD_PARAM_BIAS_OFFUV6,
   BRW_SAMPLER_PAYLOAD_PARAM_BIAS_OFFUVR4,
   BRW_SAMPLER_PAYLOAD_PARAM_LOD_AI,
   BRW_SAMPLER_PAYLOAD_PARAM_LOD_OFFUV6,
   BRW_SAMPLER_PAYLOAD_PARAM_LOD_OFFUVR4,
   BRW_SAMPLER_PAYLOAD_PARAM_MLOD_R,
   BRW_SAMPLER_PAYLOAD_PARAM_SI,
   BRW_SAMPLER_PAYLOAD_PARAM_SSI,
   BRW_SAMPLER_PAYLOAD_PARAM_MCS,
   BRW_SAMPLER_PAYLOAD_PARAM_MCSL,
   BRW_SAMPLER_PAYLOAD_PARAM_MCSH,
   BRW_SAMPLER_PAYLOAD_PARAM_MCS0,
   BRW_SAMPLER_PAYLOAD_PARAM_MCS1,
   BRW_SAMPLER_PAYLOAD_PARAM_MCS2,
   BRW_SAMPLER_PAYLOAD_PARAM_MCS3,
};

enum ENUM_PACKED brw_sampler_opcode {
   BRW_SAMPLER_OPCODE_SAMPLE_LZ,
   BRW_SAMPLER_OPCODE_SAMPLE,
   BRW_SAMPLER_OPCODE_SAMPLE_B,
   BRW_SAMPLER_OPCODE_SAMPLE_B_PACKED,
   BRW_SAMPLER_OPCODE_SAMPLE_B_REDUCED,
   BRW_SAMPLER_OPCODE_SAMPLE_C_LZ,
   BRW_SAMPLER_OPCODE_SAMPLE_C,
   BRW_SAMPLER_OPCODE_SAMPLE_D,
   BRW_SAMPLER_OPCODE_SAMPLE_D_REDUCED,
   BRW_SAMPLER_OPCODE_SAMPLE_D_C,
   BRW_SAMPLER_OPCODE_SAMPLE_D_C_PACKED,
   BRW_SAMPLER_OPCODE_SAMPLE_L,
   BRW_SAMPLER_OPCODE_SAMPLE_L_PACKED,
   BRW_SAMPLER_OPCODE_SAMPLE_L_REDUCED,
   BRW_SAMPLER_OPCODE_SAMPLE_B_C,
   BRW_SAMPLER_OPCODE_SAMPLE_B_C_PACKED,
   BRW_SAMPLER_OPCODE_SAMPLE_L_C,
   BRW_SAMPLER_OPCODE_SAMPLE_L_C_PACKED,
   BRW_SAMPLER_OPCODE_LD_LZ,
   BRW_SAMPLER_OPCODE_LD,
   BRW_SAMPLER_OPCODE_LOD,
   BRW_SAMPLER_OPCODE_RESINFO,
   BRW_SAMPLER_OPCODE_SAMPLEINFO,
   BRW_SAMPLER_OPCODE_GATHER4,
   BRW_SAMPLER_OPCODE_GATHER4_B,
   BRW_SAMPLER_OPCODE_GATHER4_C,
   BRW_SAMPLER_OPCODE_GATHER4_I,
   BRW_SAMPLER_OPCODE_GATHER4_I_C,
   BRW_SAMPLER_OPCODE_GATHER4_L,
   BRW_SAMPLER_OPCODE_GATHER4_L_C,
   BRW_SAMPLER_OPCODE_GATHER4_PO,
   BRW_SAMPLER_OPCODE_GATHER4_PO_PACKED,
   BRW_SAMPLER_OPCODE_GATHER4_PO_B,
   BRW_SAMPLER_OPCODE_GATHER4_PO_C,
   BRW_SAMPLER_OPCODE_GATHER4_PO_C_PACKED,
   BRW_SAMPLER_OPCODE_GATHER4_PO_L,
   BRW_SAMPLER_OPCODE_GATHER4_PO_L_C,
   BRW_SAMPLER_OPCODE_LD2DMS_W,
   BRW_SAMPLER_OPCODE_LD2DMS_W_GFX125,
   //BRW_SAMPLER_OPCODE_LD2DMS_W_GFX12,
   BRW_SAMPLER_OPCODE_LD_MCS,
   BRW_SAMPLER_OPCODE_LD2DMS,
   BRW_SAMPLER_OPCODE_LD2DSS,

   BRW_SAMPLER_OPCODE_MAX,
};

struct brw_sampler_payload_src {
   enum brw_sampler_payload_param param;
   bool optional;
};

struct brw_sampler_payload_desc {
   struct brw_sampler_payload_src sources[12];
};

const char *
brw_sampler_payload_param_name(enum brw_sampler_payload_param param);

const char *
brw_sampler_opcode_name(enum brw_sampler_opcode opcode);

const struct brw_sampler_payload_desc *
brw_get_sampler_payload_desc(enum brw_sampler_opcode opcode);

uint32_t
brw_get_sampler_hw_opcode(enum brw_sampler_opcode opcode);

enum brw_sampler_opcode
brw_get_sampler_opcode_from_tex(const struct intel_device_info *devinfo,
                                const nir_tex_instr *tex);

bool
brw_sampler_opcode_is_gather(enum brw_sampler_opcode opcode);

static inline int
brw_sampler_opcode_param_index(enum brw_sampler_opcode opcode,
                               enum brw_sampler_payload_param param)
{
   const struct brw_sampler_payload_desc *desc =
      brw_get_sampler_payload_desc(opcode);

   for (int i = 0; desc->sources[i].param != BRW_SAMPLER_PAYLOAD_PARAM_INVALID; i++) {
      if (desc->sources[i].param == param)
         return i;
   }

   return -1;
}

bool
brw_nir_tex_offset_in_constant_range(const nir_tex_instr *tex,
                                     unsigned offset_index);

#ifdef __cplusplus
}
#endif
