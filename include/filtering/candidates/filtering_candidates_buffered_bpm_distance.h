/*
 *  GEM-Mapper v3 (GEM3)
 *  Copyright (c) 2011-2017 by Santiago Marco-Sola  <santiagomsola@gmail.com>
 *
 *  This file is part of GEM-Mapper v3 (GEM3).
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * PROJECT: GEM-Mapper v3 (GEM3)
 * AUTHOR(S): Santiago Marco-Sola <santiagomsola@gmail.com>
 * DESCRIPTION:
 *   Filtering candidates module provides functions to verify filtering-regions
 *   against its corresponding region of text in the index and compute the
 *   distance of the alignment between both
 *   This "buffered" module operates in batches of filtering-regions and
 *   makes use of GPU-buffers to offload the verification/alignment of
 *   regions to a GPU
 */

#include "filtering/candidates/filtering_candidates.h"
#include "filtering/candidates/filtering_candidates_buffered.h"
#include "align/pattern/pattern.h"
#include "gpu/gpu_buffer_bpm_distance.h"

/*
 * BPM-Distance Buffered Add (Candidates Verification)
 */
void filtering_candidates_buffered_bpm_distance_add(
    filtering_candidates_t* const filtering_candidates,
    filtering_candidates_buffered_t* const filtering_candidates_buffered,
    pattern_t* const pattern,
    gpu_buffer_bpm_distance_t* const gpu_buffer_bpm_distance,
    uint64_t* const gpu_buffer_align_offset);

/*
 * BPM-Distance Buffered Retrieve (Candidates Verification)
 */
void filtering_candidates_buffered_bpm_distance_retrieve(
    filtering_candidates_t* const filtering_candidates,
    filtering_candidates_buffered_t* const filtering_candidates_buffered,
    pattern_t* const pattern,
    gpu_buffer_bpm_distance_t* const gpu_buffer_bpm_distance,
    const uint64_t gpu_buffer_bpm_distance_offset);
