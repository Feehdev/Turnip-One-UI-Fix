/*
 * Copyright © 2023 Valve Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include "ac_nir.h"
#include "nir_builder.h"

/**
 * Build a manual selection sequence for cube face sc/tc coordinates and
 * major axis vector (multiplied by 2 for consistency) for the given
 * vec3 \p coords, for the face implied by \p selcoords.
 *
 * For the major axis, we always adjust the sign to be in the direction of
 * selcoords.ma; i.e., a positive out_ma means that coords is pointed towards
 * the selcoords major axis.
 */
static void
build_cube_select(nir_builder *b, nir_def *ma, nir_def *id, nir_def *deriv,
                  nir_def **out_ma, nir_def **out_sc, nir_def **out_tc)
{
   nir_def *deriv_x = nir_channel(b, deriv, 0);
   nir_def *deriv_y = nir_channel(b, deriv, 1);
   nir_def *deriv_z = nir_channel(b, deriv, 2);

   nir_def *is_ma_positive = nir_fge_imm(b, ma, 0.0);
   nir_def *sgn_ma =
      nir_bcsel(b, is_ma_positive, nir_imm_float(b, 1.0), nir_imm_float(b, -1.0));
   nir_def *neg_sgn_ma = nir_fneg(b, sgn_ma);

   nir_def *is_ma_z = nir_fge_imm(b, id, 4.0);
   nir_def *is_ma_y = nir_fge_imm(b, id, 2.0);
   is_ma_y = nir_iand(b, is_ma_y, nir_inot(b, is_ma_z));
   nir_def *is_not_ma_x = nir_ior(b, is_ma_z, is_ma_y);

   /* Select sc */
   nir_def *tmp = nir_bcsel(b, is_not_ma_x, deriv_x, deriv_z);
   nir_def *sgn =
      nir_bcsel(b, is_ma_y, nir_imm_float(b, 1.0), nir_bcsel(b, is_ma_z, sgn_ma, neg_sgn_ma));
   *out_sc = nir_fmul(b, tmp, sgn);

   /* Select tc */
   tmp = nir_bcsel(b, is_ma_y, deriv_z, deriv_y);
   sgn = nir_bcsel(b, is_ma_y, sgn_ma, nir_imm_float(b, -1.0));
   *out_tc = nir_fmul(b, tmp, sgn);

   /* Select ma */
   tmp = nir_bcsel(b, is_ma_z, deriv_z, nir_bcsel(b, is_ma_y, deriv_y, deriv_x));
   *out_ma = nir_fmul_imm(b, nir_fabs(b, tmp), 2.0);
}

static void
prepare_cube_coords(nir_builder *b, nir_tex_instr *tex, nir_def **coord, nir_src *ddx,
                    nir_src *ddy, const ac_nir_lower_tex_options *options)
{
   nir_def *coords[NIR_MAX_VEC_COMPONENTS] = {0};
   for (unsigned i = 0; i < (*coord)->num_components; i++)
      coords[i] = nir_channel(b, *coord, i);

   /* Section 8.9 (Texture Functions) of the GLSL 4.50 spec says:
    *
    *    "For Array forms, the array layer used will be
    *
    *       max(0, min(d−1, floor(layer+0.5)))
    *
    *     where d is the depth of the texture array and layer
    *     comes from the component indicated in the tables below.
    *     Workaroudn for an issue where the layer is taken from a
    *     helper invocation which happens to fall on a different
    *     layer due to extrapolation."
    *
    * GFX8 and earlier attempt to implement this in hardware by
    * clamping the value of coords[2] = (8 * layer) + face.
    * Unfortunately, this means that the we end up with the wrong
    * face when clamping occurs.
    *
    * Clamp the layer earlier to work around the issue.
    */
   if (tex->is_array && options->gfx_level <= GFX8 && coords[3])
      coords[3] = nir_fmax(b, coords[3], nir_imm_float(b, 0.0));

   nir_def *cube_coords = nir_cube_amd(b, nir_vec(b, coords, 3));
   nir_def *sc = nir_channel(b, cube_coords, 1);
   nir_def *tc = nir_channel(b, cube_coords, 0);
   nir_def *ma = nir_channel(b, cube_coords, 2);
   nir_def *invma = nir_frcp(b, nir_fabs(b, ma));
   nir_def *id = nir_channel(b, cube_coords, 3);

   if (ddx || ddy) {
      sc = nir_fmul(b, sc, invma);
      tc = nir_fmul(b, tc, invma);

      /* Convert cube derivatives to 2D derivatives. */
      for (unsigned i = 0; i < 2; i++) {
         /* Transform the derivative alongside the texture
          * coordinate. Mathematically, the correct formula is
          * as follows. Assume we're projecting onto the +Z face
          * and denote by dx/dh the derivative of the (original)
          * X texture coordinate with respect to horizontal
          * window coordinates. The projection onto the +Z face
          * plane is:
          *
          *   f(x,z) = x/z
          *
          * Then df/dh = df/dx * dx/dh + df/dz * dz/dh
          *            = 1/z * dx/dh - x/z * 1/z * dz/dh.
          *
          * This motivatives the implementation below.
          *
          * Whether this actually gives the expected results for
          * apps that might feed in derivatives obtained via
          * finite differences is anyone's guess. The OpenGL spec
          * seems awfully quiet about how textureGrad for cube
          * maps should be handled.
          */
         nir_def *deriv_ma, *deriv_sc, *deriv_tc;
         build_cube_select(b, ma, id, i ? ddy->ssa : ddx->ssa, &deriv_ma, &deriv_sc, &deriv_tc);

         deriv_ma = nir_fmul(b, deriv_ma, invma);

         nir_def *x = nir_fsub(b, nir_fmul(b, deriv_sc, invma), nir_fmul(b, deriv_ma, sc));
         nir_def *y = nir_fsub(b, nir_fmul(b, deriv_tc, invma), nir_fmul(b, deriv_ma, tc));

         nir_src_rewrite(i ? ddy : ddx, nir_vec2(b, x, y));
      }

      sc = nir_fadd_imm(b, sc, 1.5);
      tc = nir_fadd_imm(b, tc, 1.5);
   } else {
      sc = nir_ffma_imm2(b, sc, invma, 1.5);
      tc = nir_ffma_imm2(b, tc, invma, 1.5);
   }

   if (tex->is_array && coords[3])
      id = nir_ffma_imm1(b, coords[3], 8.0, id);

   *coord = nir_vec3(b, sc, tc, id);

   tex->is_array = true;
}

static bool
lower_array_layer_round_even(nir_builder *b, nir_tex_instr *tex, nir_def **coords)
{
   int coord_index = nir_tex_instr_src_index(tex, nir_tex_src_coord);
   if (coord_index < 0 || nir_tex_instr_src_type(tex, coord_index) != nir_type_float)
      return false;

   unsigned layer = tex->coord_components - 1;
   nir_def *rounded_layer = nir_fround_even(b, nir_channel(b, *coords, layer));
   *coords = nir_vector_insert_imm(b, *coords, rounded_layer, layer);
   return true;
}

static bool
lower_tex_coords(nir_builder *b, nir_tex_instr *tex, nir_def **coords,
                 const ac_nir_lower_tex_options *options)
{
   bool progress = false;
   if ((options->lower_array_layer_round_even || tex->sampler_dim == GLSL_SAMPLER_DIM_CUBE) &&
       tex->is_array && tex->op != nir_texop_lod)
      progress |= lower_array_layer_round_even(b, tex, coords);

   if (tex->sampler_dim != GLSL_SAMPLER_DIM_CUBE)
      return progress;

   int ddx_idx = nir_tex_instr_src_index(tex, nir_tex_src_ddx);
   int ddy_idx = nir_tex_instr_src_index(tex, nir_tex_src_ddy);
   nir_src *ddx = ddx_idx >= 0 ? &tex->src[ddx_idx].src : NULL;
   nir_src *ddy = ddy_idx >= 0 ? &tex->src[ddy_idx].src : NULL;

   prepare_cube_coords(b, tex, coords, ddx, ddy, options);

   return true;
}

static bool
lower_tex(nir_builder *b, nir_instr *instr, void *options_)
{
   const ac_nir_lower_tex_options *options = options_;
   if (instr->type != nir_instr_type_tex)
      return false;

   nir_tex_instr *tex = nir_instr_as_tex(instr);
   int coord_idx = nir_tex_instr_src_index(tex, nir_tex_src_coord);
   if (coord_idx < 0 || nir_tex_instr_src_index(tex, nir_tex_src_backend1) >= 0)
      return false;

   b->cursor = nir_before_instr(instr);
   nir_def *coords = tex->src[coord_idx].src.ssa;
   if (lower_tex_coords(b, tex, &coords, options)) {
      tex->coord_components = coords->num_components;
      nir_src_rewrite(&tex->src[coord_idx].src, coords);
      return true;
   }

   return false;
}

typedef struct {
   nir_intrinsic_instr *bary;
   nir_intrinsic_instr *load;
} coord_info;

static bool can_move_coord(nir_scalar scalar, coord_info *info, nir_block *toplevel_block, bool txd)
{
   if (scalar.def->bit_size != 32)
      return false;

   /* Allow any def that is reachable from the nir_strict_wqm_coord_amd when
    * optimizing nir_texop_txd. Otherwise, we only use nir_strict_wqm_coord_amd
    * for cases that D3D11 requires.
    */
   if (txd && nir_block_dominates(scalar.def->parent_instr->block, toplevel_block)) {
      info->load = NULL;
      return true;
   }

   if (nir_scalar_is_const(scalar))
      return true;

   if (!nir_scalar_is_intrinsic(scalar))
      return false;

   nir_intrinsic_instr *intrin = nir_def_as_intrinsic(scalar.def);
   if (intrin->intrinsic == nir_intrinsic_load_input ||
       intrin->intrinsic == nir_intrinsic_load_per_primitive_input) {
      info->bary = NULL;
      info->load = intrin;
      return true;
   }

   if (intrin->intrinsic != nir_intrinsic_load_interpolated_input)
      return false;

   nir_scalar coord_x = nir_scalar_resolved(intrin->src[0].ssa, 0);
   nir_scalar coord_y = nir_scalar_resolved(intrin->src[0].ssa, 1);
   if (!nir_scalar_is_intrinsic(coord_x) || coord_x.comp != 0 ||
       !nir_scalar_is_intrinsic(coord_y) || coord_y.comp != 1)
      return false;

   nir_intrinsic_instr *intrin_x = nir_def_as_intrinsic(coord_x.def);
   nir_intrinsic_instr *intrin_y = nir_def_as_intrinsic(coord_y.def);
   if (intrin_x->intrinsic != intrin_y->intrinsic ||
       (intrin_x->intrinsic != nir_intrinsic_load_barycentric_sample &&
        intrin_x->intrinsic != nir_intrinsic_load_barycentric_pixel &&
        intrin_x->intrinsic != nir_intrinsic_load_barycentric_centroid) ||
       nir_intrinsic_interp_mode(intrin_x) != nir_intrinsic_interp_mode(intrin_y))
      return false;

   info->bary = intrin_x;
   info->load = intrin;

   return true;
}

struct move_tex_coords_state {
   const ac_nir_lower_tex_options *options;
   unsigned num_wqm_vgprs;
   nir_builder toplevel_b;
};

struct loop_if_state {
   bool inside_loop;
   unsigned prev_terminate;
   unsigned prev_break_continue;
};

static nir_def *
build_coordinate(struct move_tex_coords_state *state, nir_scalar scalar, coord_info info)
{
   nir_builder *b = &state->toplevel_b;

   if (nir_scalar_is_const(scalar))
      return nir_imm_intN_t(b, nir_scalar_as_uint(scalar), scalar.def->bit_size);

   if (!info.load)
      return nir_mov_scalar(b, scalar);

   ASSERTED nir_src offset = *nir_get_io_offset_src(info.load);
   assert(nir_src_is_const(offset) && !nir_src_as_uint(offset));

   nir_def *zero = nir_imm_int(b, 0);
   nir_def *res;
   if (info.bary) {
      enum glsl_interp_mode interp_mode = nir_intrinsic_interp_mode(info.bary);
      nir_def *bary = nir_load_system_value(b, info.bary->intrinsic, interp_mode, 2, 32);
      res = nir_load_interpolated_input(b, 1, 32, bary, zero);
   } else {
      res = nir_load_input(b, 1, 32, zero);
   }
   nir_intrinsic_instr *intrin = nir_def_as_intrinsic(res);
   nir_intrinsic_set_base(intrin, nir_intrinsic_base(info.load));
   nir_intrinsic_set_component(intrin, nir_intrinsic_component(info.load) + scalar.comp);
   nir_intrinsic_set_dest_type(intrin, nir_intrinsic_dest_type(info.load));
   nir_intrinsic_set_io_semantics(intrin, nir_intrinsic_io_semantics(info.load));
   return res;
}

static bool can_optimize_txd(nir_shader *shader, struct loop_if_state *loop_if, nir_tex_instr *tex,
                             bool *need_strict_wqm_coord)
{
   nir_instr *ddxy_instrs[NIR_MAX_VEC_COMPONENTS * 2];
   unsigned size = nir_tex_parse_txd_coords(shader, tex, ddxy_instrs);
   if (!size)
      return false;

   bool incomplete_quad =
      tex->instr.block->divergent || loop_if->prev_terminate || loop_if->inside_loop;

   *need_strict_wqm_coord = false;
   if (incomplete_quad) {
      for (unsigned i = 0; i < size; i++) {
         nir_instr *instr = ddxy_instrs[i];
         *need_strict_wqm_coord |=
            instr->block->cf_node.parent != tex->instr.block->cf_node.parent ||
            loop_if->prev_terminate > instr->index || loop_if->prev_break_continue > instr->index;
      }
   }

   return true;
}

static bool optimize_txd(nir_tex_instr *tex)
{
   if (tex->op == nir_texop_txd) {
      tex->op = nir_texop_tex;
      nir_tex_instr_remove_src(tex, nir_tex_instr_src_index(tex, nir_tex_src_ddx));
      nir_tex_instr_remove_src(tex, nir_tex_instr_src_index(tex, nir_tex_src_ddy));
      return true;
   }

   return false;
}

static bool
move_tex_coords(struct move_tex_coords_state *state, nir_function_impl *impl, nir_instr *instr)
{
   nir_tex_instr *tex = nir_instr_as_tex(instr);
   if (tex->op != nir_texop_tex && tex->op != nir_texop_txb && tex->op != nir_texop_lod &&
       tex->op != nir_texop_txd)
      return false;

   switch (tex->sampler_dim) {
   case GLSL_SAMPLER_DIM_1D:
   case GLSL_SAMPLER_DIM_2D:
   case GLSL_SAMPLER_DIM_3D:
   case GLSL_SAMPLER_DIM_CUBE:
   case GLSL_SAMPLER_DIM_EXTERNAL:
      break;
   case GLSL_SAMPLER_DIM_RECT:
   case GLSL_SAMPLER_DIM_BUF:
   case GLSL_SAMPLER_DIM_MS:
   case GLSL_SAMPLER_DIM_SUBPASS:
   case GLSL_SAMPLER_DIM_SUBPASS_MS:
      return false; /* No LOD or can't be sampled. */
   }

   if (nir_tex_instr_src_index(tex, nir_tex_src_min_lod) != -1)
      return false;

   nir_tex_src *src = &tex->src[nir_tex_instr_src_index(tex, nir_tex_src_coord)];
   nir_scalar components[NIR_MAX_VEC_COMPONENTS];
   coord_info infos[NIR_MAX_VEC_COMPONENTS];
   bool can_move_all = true;
   nir_block *toplevel_block = nir_cursor_current_block(state->toplevel_b.cursor);
   for (unsigned i = 0; i < tex->coord_components; i++) {
      components[i] = nir_scalar_resolved(src->src.ssa, i);
      can_move_all &=
         can_move_coord(components[i], &infos[i], toplevel_block, tex->op == nir_texop_txd);
   }
   if (!can_move_all)
      return false;

   int coord_base = 0;
   unsigned linear_vgpr_size = tex->coord_components;
   if (tex->sampler_dim == GLSL_SAMPLER_DIM_CUBE && tex->is_array)
      linear_vgpr_size--; /* cube array layer and face are combined */
   for (unsigned i = 0; i < tex->num_srcs; i++) {
      switch (tex->src[i].src_type) {
      case nir_tex_src_offset:
      case nir_tex_src_bias:
      case nir_tex_src_comparator:
         coord_base++;
         linear_vgpr_size++;
         break;
      default:
         break;
      }
   }

   if (state->num_wqm_vgprs + linear_vgpr_size > state->options->max_wqm_vgprs)
      return false;

   for (unsigned i = 0; i < tex->coord_components; i++)
      components[i] = nir_get_scalar(build_coordinate(state, components[i], infos[i]), 0);

   nir_def *linear_vgpr = nir_vec_scalars(&state->toplevel_b, components, tex->coord_components);
   lower_tex_coords(&state->toplevel_b, tex, &linear_vgpr, state->options);

   linear_vgpr = nir_strict_wqm_coord_amd(&state->toplevel_b, linear_vgpr, coord_base * 4);

   nir_tex_instr_remove_src(tex, nir_tex_instr_src_index(tex, nir_tex_src_coord));
   tex->coord_components = 0;

   nir_tex_instr_add_src(tex, nir_tex_src_backend1, linear_vgpr);

   int offset_src = nir_tex_instr_src_index(tex, nir_tex_src_offset);
   if (offset_src >= 0) /* Workaround requirement in nir_tex_instr_src_size(). */
      tex->src[offset_src].src_type = nir_tex_src_backend2;

   optimize_txd(tex);

   state->num_wqm_vgprs += linear_vgpr_size;

   return true;
}

static bool
move_ddxy(struct move_tex_coords_state *state, nir_function_impl *impl, nir_intrinsic_instr *instr)
{
   unsigned num_components = instr->def.num_components;
   nir_scalar components[NIR_MAX_VEC_COMPONENTS];
   coord_info infos[NIR_MAX_VEC_COMPONENTS];
   bool can_move_all = true;
   for (unsigned i = 0; i < num_components; i++) {
      components[i] = nir_scalar_resolved(instr->src[0].ssa, i);
      can_move_all &= can_move_coord(components[i], &infos[i], NULL, false);
   }
   if (!can_move_all || state->num_wqm_vgprs + num_components > state->options->max_wqm_vgprs)
      return false;

   for (unsigned i = 0; i < num_components; i++) {
      nir_def *def = build_coordinate(state, components[i], infos[i]);
      components[i] = nir_get_scalar(def, 0);
   }

   nir_def *def = nir_vec_scalars(&state->toplevel_b, components, num_components);
   def = _nir_build_ddx(&state->toplevel_b, def->bit_size, def);
   nir_def_as_intrinsic(def)->intrinsic = instr->intrinsic;
   nir_def_rewrite_uses(&instr->def, def);

   state->num_wqm_vgprs += num_components;

   return true;
}

static bool move_coords_from_divergent_cf(struct move_tex_coords_state *state,
                                          struct loop_if_state *loop_if, struct exec_list *cf_list)
{
   nir_function_impl *impl = state->toplevel_b.impl;
   nir_shader *shader = impl->function->shader;

   bool progress = false;
   foreach_list_typed (nir_cf_node, cf_node, node, cf_list) {
      switch (cf_node->type) {
      case nir_cf_node_block: {
         nir_block *block = nir_cf_node_as_block(cf_node);

         bool top_level = cf_list == &impl->body;

         nir_foreach_instr (instr, block) {
            if (top_level && !loop_if->prev_terminate)
               state->toplevel_b.cursor = nir_before_instr(instr);

            /* Assume quads might be incomplete when inside loops in case of a
             * divergent terminate from a previous iteration.
             */
            bool incomplete_quad =
               block->divergent || loop_if->prev_terminate || loop_if->inside_loop;

            if (instr->type == nir_instr_type_tex) {
               nir_tex_instr *tex = nir_instr_as_tex(instr);

               if (tex->op == nir_texop_txd) {
                  bool txd_need_strict_wqm_coord = false;
                  if (!can_optimize_txd(shader, loop_if, tex, &txd_need_strict_wqm_coord))
                     continue;
                  if (!txd_need_strict_wqm_coord)
                     progress |= optimize_txd(tex);
               }

               if (state->options->fix_derivs_in_divergent_cf && incomplete_quad)
                  progress |= move_tex_coords(state, impl, instr);
            } else if (instr->type == nir_instr_type_intrinsic) {
               nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
               switch (intrin->intrinsic) {
               case nir_intrinsic_terminate:
                  if (block->divergent)
                     loop_if->prev_terminate = instr->index;
                  break;
               case nir_intrinsic_terminate_if:
                  if (block->divergent || nir_src_is_divergent(&intrin->src[0]))
                     loop_if->prev_terminate = instr->index;
                  break;
               case nir_intrinsic_ddx:
               case nir_intrinsic_ddy:
               case nir_intrinsic_ddx_fine:
               case nir_intrinsic_ddy_fine:
               case nir_intrinsic_ddx_coarse:
               case nir_intrinsic_ddy_coarse:
                  if (incomplete_quad)
                     progress |= move_ddxy(state, impl, intrin);
                  break;
               default:
                  break;
               }
            } else if (instr->type == nir_instr_type_jump && block->divergent) {
               loop_if->prev_break_continue = instr->index;
            }
         }

         if (top_level && !loop_if->prev_terminate)
            state->toplevel_b.cursor = nir_after_block_before_jump(block);
         break;
      }
      case nir_cf_node_if: {
         nir_if *nif = nir_cf_node_as_if(cf_node);
         struct loop_if_state inner_then = *loop_if;
         struct loop_if_state inner_else = *loop_if;
         progress |= move_coords_from_divergent_cf(state, &inner_then, &nif->then_list);
         progress |= move_coords_from_divergent_cf(state, &inner_else, &nif->else_list);
         loop_if->prev_terminate = MAX2(inner_then.prev_terminate, inner_else.prev_terminate);
         loop_if->prev_break_continue =
            MAX2(inner_then.prev_break_continue, inner_else.prev_break_continue);
         break;
      }
      case nir_cf_node_loop: {
         nir_loop *loop = nir_cf_node_as_loop(cf_node);
         assert(!nir_loop_has_continue_construct(loop));
         struct loop_if_state inner = *loop_if;
         inner.inside_loop = true;
         progress |= move_coords_from_divergent_cf(state, &inner, &loop->body);
         loop_if->prev_terminate = inner.prev_terminate;
         break;
      }
      case nir_cf_node_function:
         UNREACHABLE("Invalid cf type");
      }
   }

   return progress;
}

bool
ac_nir_lower_tex(nir_shader *nir, const ac_nir_lower_tex_options *options)
{
   bool progress = false;
   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      nir_function_impl *impl = nir_shader_get_entrypoint(nir);
      nir_metadata_require(
         impl, nir_metadata_divergence | nir_metadata_dominance | nir_metadata_instr_index);

      struct move_tex_coords_state state;
      state.toplevel_b = nir_builder_create(impl);
      state.options = options;
      state.num_wqm_vgprs = 0;

      struct loop_if_state loop_if;
      loop_if.inside_loop = false;
      loop_if.prev_terminate = 0;
      loop_if.prev_break_continue = 0;
      bool impl_progress = move_coords_from_divergent_cf(&state, &loop_if, &impl->body);
      progress |= nir_progress(impl_progress, impl, nir_metadata_control_flow);
   }

   progress |= nir_shader_instructions_pass(
      nir, lower_tex, nir_metadata_control_flow, (void *)options);

   return progress;
}
