/*
 * PROJECT: GEMMapper
 * FILE: filtering_candidates_process.c
 * DATE: 06/06/2013
 * AUTHOR(S): Santiago Marco-Sola <santiagomsola@gmail.com>
 * DESCRIPTION:
 */

#include "filtering/filtering_candidates_process.h"
#include "archive/archive_text_rl.h"
#include "archive/sampled_rl.h"

/*
 * Debug
 */
#define DEBUG_FILTERING_CANDIDATES                     GEM_DEEP_DEBUG
#define DEBUG_FILTERING_CANDIDATES_REGIONS_MATCHING    false // GEM_DEEP_DEBUG too verbose

/*
 * Profile
 */
#define PROFILE_LEVEL PMED

/*
 * Add filtering region from one or many filtering positions (and compose matching regions)
 */
void filtering_candidates_add_filtering_region(
    filtering_candidates_t* const filtering_candidates,
    const uint64_t first_candidate_idx,
    const uint64_t last_candidate_idx,
    const uint64_t align_distance,
    const bool compose_region_chaining) {
  // Fetch candidate-positions
  filtering_position_t* const candidate_positions =
      vector_get_mem(filtering_candidates->filtering_positions,filtering_position_t);
  // Allow new matching candidate-region
  filtering_region_t* filtering_region;
  vector_alloc_new(filtering_candidates->filtering_regions,filtering_region_t,filtering_region);
  // State
  filtering_region->status = filtering_region_unverified; // Newly created region (unverified)
  // Location
  filtering_position_t* const first_candidate = candidate_positions + first_candidate_idx;
  filtering_position_t* const last_candidate = candidate_positions + last_candidate_idx;
  filtering_region->text_trace_offset = UINT64_MAX; // Unassigned
  filtering_region->begin_position = first_candidate->begin_position;
  filtering_region->end_position = last_candidate->end_position;
  filtering_region->base_begin_position_offset = first_candidate->base_begin_position_offset;
  const uint64_t last_candidate_base_end_position = last_candidate->begin_position + last_candidate->base_end_position_offset;
  filtering_region->base_end_position_offset = last_candidate_base_end_position - first_candidate->begin_position;
  // Trimmed Pattern
  filtering_region->key_trim_left = first_candidate->key_trim_left;
  filtering_region->key_trim_right = first_candidate->key_trim_right;
  filtering_region->key_trimmed = (filtering_region->key_trim_left>0 || filtering_region->key_trim_right>0);
  filtering_region->bpm_pattern_trimmed = NULL;
  filtering_region->bpm_pattern_trimmed_tiles = NULL;
  PROF_ADD_COUNTER(GP_CANDIDATE_REGION_LENGTH,filtering_region->end_position-filtering_region->begin_position);
  // Alignment distance
  filtering_region->align_distance = align_distance;
  // Matching Regions (Compose regions matching)
  match_scaffold_t* const matches_scaffold = &filtering_region->match_scaffold;
  match_scaffold_init(matches_scaffold);
  if (compose_region_chaining) {
    mm_stack_t* const mm_stack = filtering_candidates->mm_stack;
    const uint64_t num_regions_matching = last_candidate_idx-first_candidate_idx+1;
    matches_scaffold->scaffold_regions = mm_stack_calloc(mm_stack,num_regions_matching,region_matching_t,false);
    matches_scaffold->num_scaffold_regions = num_regions_matching;
    uint64_t i;
    for (i=0;i<num_regions_matching;++i) {
      region_matching_t* const region_matching = matches_scaffold->scaffold_regions + i;
      filtering_position_t* const candidate_position = candidate_positions + first_candidate_idx + i;
      region_search_t* const source_region = vector_get_elm(
          filtering_candidates->regions_buffer,candidate_position->source_region_offset,region_search_t);
      // Region error
      region_matching->matching_type = (source_region->degree==0) ? region_matching_exact : region_matching_approximate;
      region_matching->error = source_region->degree;
      region_matching->cigar_length = 0;
      // Read coordinates
      region_matching->key_begin = source_region->begin;
      region_matching->key_end = source_region->end;
      // Text coordinates (relative to the effective begin position)
      const uint64_t region_length = region_matching->key_end - region_matching->key_begin;
      region_matching->text_begin = candidate_position->region_text_position - filtering_region->begin_position;
      region_matching->text_end = region_matching->text_begin + region_length;
    }
  }
}
uint64_t filtering_candidates_compose_filtering_regions(
    filtering_candidates_t* const filtering_candidates,
    const uint64_t key_length,
    const uint64_t max_delta_difference,
    const bool compose_region_chaining) {
  // Sort candidate positions (text-space)
  filtering_positions_sort_positions(filtering_candidates->filtering_positions);
  // Sort verified regions
  verified_regions_sort_positions(filtering_candidates->verified_regions);
  const uint64_t num_verified_regions = vector_get_used(filtering_candidates->verified_regions);
  verified_region_t* const verified_region = vector_get_mem(filtering_candidates->verified_regions,verified_region_t);
  // Traverse positions and eliminate duplicates
  const uint64_t num_candidate_positions = vector_get_used(filtering_candidates->filtering_positions);
  filtering_position_t* const candidate_positions =
      vector_get_mem(filtering_candidates->filtering_positions,filtering_position_t);
  uint64_t candidate_idx = 0, verified_region_idx = 0;
  while (candidate_idx < num_candidate_positions) {
    // Determine the positions belonging to the same region
    const uint64_t sequence_id = candidate_positions[candidate_idx].sequence_id;
    const uint64_t region_begin_position = candidate_positions[candidate_idx].begin_position;
    uint64_t min_align_distance = candidate_positions[candidate_idx].align_distance;
    uint64_t group_idx = candidate_idx + 1;
    while (group_idx < num_candidate_positions) {
      // Check delta-difference between candidate-positions
      const uint64_t position = candidate_positions[group_idx].begin_position;
      const uint64_t delta = position - region_begin_position;
      if (sequence_id != candidate_positions[group_idx].sequence_id) break; // Different sequence. Stop!
      if (delta > max_delta_difference) break; // Doesn't belong to the group. Stop!
      // Select the best already-known alignment distance
      if (candidate_positions[group_idx].align_distance < min_align_distance) {
        min_align_distance = candidate_positions[group_idx].align_distance;
      }
      // Next
      ++group_idx;
    }
    // Check region against verified regions
    bool is_already_verified = false;
    while (verified_region_idx < num_verified_regions &&
           verified_region[verified_region_idx].end_position <= region_begin_position) {
      ++verified_region_idx;
    }
    if (verified_region_idx < num_verified_regions) {
      const uint64_t verified_begin_position = verified_region[verified_region_idx].begin_position;
      const uint64_t verified_end_position = verified_region[verified_region_idx].end_position;
      const uint64_t region_end_position = candidate_positions[group_idx-1].end_position;
      is_already_verified = (verified_begin_position <= region_begin_position &&
                             region_end_position <= verified_end_position);
      gem_debug_block() {
        if (is_already_verified) {
          PROF_INC_COUNTER(GP_CANDIDATE_REGIONS_DUPLICATED);
        }
      }
    }
    if (!is_already_verified) {
      // Create a region candidate with the positions from [candidate_idx] to [group_idx-1]
      filtering_candidates_add_filtering_region(
          filtering_candidates,candidate_idx,group_idx-1,
          min_align_distance,compose_region_chaining);
    }
    // Next group
    const uint64_t num_regions_matching = group_idx-candidate_idx;
    candidate_idx += num_regions_matching;
  }
  // Clear candidate positions
  vector_clear(filtering_candidates->filtering_positions);
  // DEBUG
  gem_cond_debug_block(DEBUG_FILTERING_CANDIDATES) {
    tab_fprintf(gem_log_get_stream(),"[GEM]>Filtering.Candidates (compose_filtering_regions)\n");
    tab_global_inc();
    filtering_candidates_print_regions(gem_log_get_stream(),
        filtering_candidates,true,DEBUG_FILTERING_CANDIDATES_REGIONS_MATCHING);
    tab_global_dec();
  }
  // Return number of filtering regions generated
  return vector_get_used(filtering_candidates->filtering_regions);
}
/*
 * Adjust the filtering-position and compute the coordinates or the candidate text
 */
void filtering_candidates_compute_text_boundaries(
    filtering_position_t* const filtering_position,
    const uint64_t region_text_position,
    const uint64_t boundary_error,
    const uint64_t begin_offset,
    const uint64_t end_offset,
    const uint64_t interval_begin_position,
    const uint64_t interval_end_position) {
  // Init
  filtering_position->key_trim_left = 0;
  filtering_position->key_trim_right = 0;
  filtering_position->sequence_id = interval_begin_position; // Set ID
  // Adjust Begin Position
  uint64_t base_begin_position = region_text_position;
  if (region_text_position >= begin_offset) {
    base_begin_position -= begin_offset;
  } else {
    filtering_position->key_trim_left = begin_offset - region_text_position;
    base_begin_position = 0;
  }
  uint64_t begin_position;
  if (base_begin_position < interval_begin_position) { // Adjust by locator-interval
    filtering_position->key_trim_left += interval_begin_position - base_begin_position;
    base_begin_position = interval_begin_position; // Possible trim at the beginning
    begin_position = interval_begin_position;
  } else {
    begin_position = (base_begin_position > boundary_error) ? base_begin_position-boundary_error : 0;
    if (begin_position < interval_begin_position) { // Adjust by locator-interval
      begin_position = interval_begin_position;
    }
  }
  // Adjust End Position
  uint64_t base_end_position = region_text_position + end_offset;
  if (base_end_position > interval_end_position) {
    filtering_position->key_trim_right = base_end_position - interval_end_position;
    base_end_position = interval_end_position;
  }
  uint64_t end_position = base_end_position + boundary_error;
  if (end_position >= interval_end_position) { // Adjust by locator-interval
    end_position = interval_end_position; // Possible trim at the end
  }
  filtering_position->begin_position = begin_position;
  filtering_position->end_position = end_position;
  filtering_position->base_begin_position_offset = base_begin_position-begin_position;
  filtering_position->base_end_position_offset = base_end_position-begin_position;
}
void filtering_candidates_compute_text_coordinates(
    filtering_candidates_t* const filtering_candidates,
    filtering_position_t* const filtering_position,
    pattern_t* const pattern,
    const uint64_t begin_offset,
    const uint64_t end_offset) {
  // Parameters
  archive_t* const archive = filtering_candidates->archive;
  locator_t* const locator = archive->locator;
  archive_text_t* const archive_text = archive->text;
  // Locate position
  locator_interval_t* locator_interval = NULL;
  if (archive_text->run_length) {
    // Locate RL-text position
    const uint64_t region_text_position_rl = filtering_position->region_text_position;
    locator_interval = locator_lookup_rl_interval(locator,region_text_position_rl);
    filtering_position->locator_interval = locator_interval;
    // Compute boundaries
    filtering_candidates_compute_text_boundaries(filtering_position,
        filtering_position->region_text_position,pattern->max_effective_bandwidth,
        begin_offset,end_offset,locator_interval->rl_begin_position,locator_interval->rl_end_position);
  } else {
    // Locate text position
    const uint64_t region_text_position = filtering_position->region_text_position;
    locator_interval = locator_lookup_interval(locator,region_text_position);
    filtering_position->locator_interval = locator_interval;
    // Compute boundaries
    filtering_candidates_compute_text_boundaries(filtering_position,
        filtering_position->region_text_position,pattern->max_effective_bandwidth,
        begin_offset,end_offset,locator_interval->begin_position,locator_interval->end_position);
  }
}
/*
 * Decode of all candidate positions (index-space -> text-space)
 */
void filtering_candidates_decode_filtering_positions(
    filtering_candidates_t* const filtering_candidates,
    pattern_t* const pattern) {
  // Parameters
  fm_index_t* const fm_index = filtering_candidates->archive->fm_index;
  // Traverse all candidate positions in index-space
  vector_t* const candidate_text_positions = filtering_candidates->filtering_positions;
  VECTOR_ITERATE(candidate_text_positions,filtering_position,n,filtering_position_t) {
    // Lookup Position
    filtering_position->region_text_position = fm_index_decode(fm_index,filtering_position->region_index_position);
    // Adjust Position
    region_search_t* const source_region = vector_get_elm(filtering_candidates->regions_buffer,
        filtering_position->source_region_offset,region_search_t);
    filtering_candidates_compute_text_coordinates(
        filtering_candidates,filtering_position,pattern,
        source_region->begin,pattern->key_length-source_region->begin);
  }
}
/*
 * Batch decode of all candidate positions (index-space -> text-space)
 *   (All the steps (CSA-lookup, rankQueries) are performed with prefetch-loops)
 */
void filtering_candidates_decode_filtering_position_batch_prefetched(
    filtering_candidates_t* const filtering_candidates,
    pattern_t* const pattern) {
  // Init
  archive_t* const archive = filtering_candidates->archive;
  locator_t* const locator = archive->locator;
  fm_index_t* const fm_index = archive->fm_index;
  const bwt_t* const bwt = fm_index->bwt;
  const sampled_sa_t* const sampled_sa = fm_index->sampled_sa;
  vector_t* const candidate_text_positions = filtering_candidates->filtering_positions;
  const uint64_t num_candidate_text_positions = vector_get_used(candidate_text_positions);
  filtering_position_t* const filtering_position = vector_get_mem(candidate_text_positions,filtering_position_t);
  // Initial fill batch
  fc_batch_decode_candidate batch[DECODE_NUM_POSITIONS_PREFETCHED]; // Batch Decode
  bool is_sampled;
  uint64_t current_position, LF, i;
  for (i=0,current_position=0;i<DECODE_NUM_POSITIONS_PREFETCHED && current_position<num_candidate_text_positions;++current_position) {
    LF = bwt_LF(bwt,filtering_position[current_position].region_index_position,&is_sampled);
    if (!is_sampled) {
      batch[i].index_position = LF;
      batch[i].vector_rank = current_position;
      batch[i].distance = 0;
      batch[i].used_slot = true;
      ++i;
    } else {
      filtering_position[current_position].decode_sampled_pos = LF;
      filtering_position[current_position].decode_distance = 0; PROF_ADD_COUNTER(GP_FMIDX_LOOKUP_DIST,0);
    }
  }
  const bool full_filled_batch = (i==DECODE_NUM_POSITIONS_PREFETCHED);
  for (;i<DECODE_NUM_POSITIONS_PREFETCHED;++i) {
    batch[i].used_slot = false;
  }
  // Full-prefetch loop for sampled-LF
  if (full_filled_batch) {
    while (current_position<num_candidate_text_positions) {
      for (i=0;i<DECODE_NUM_POSITIONS_PREFETCHED;++i) {
        bwt_prefetch(bwt,batch[i].index_position,&(batch[i].bwt_block_locator));
      }
      for (i=0;i<DECODE_NUM_POSITIONS_PREFETCHED;++i) {
        ++(batch[i].distance);
        batch[i].index_position = bwt_prefetched_LF(bwt,batch[i].index_position,&is_sampled,&(batch[i].bwt_block_locator));
        if (is_sampled) {
          filtering_position[batch[i].vector_rank].decode_sampled_pos = batch[i].index_position;
          filtering_position[batch[i].vector_rank].decode_distance = batch[i].distance;
          PROF_ADD_COUNTER(GP_FMIDX_LOOKUP_DIST,batch[i].distance);
          batch[i].used_slot = false;
          // Select new candidate to decode
          while (current_position < num_candidate_text_positions) {
            LF = bwt_LF(bwt,filtering_position[current_position].region_index_position,&is_sampled);
            if (!is_sampled) break;
            filtering_position[current_position].decode_sampled_pos = LF;
            filtering_position[current_position].decode_distance = 0; PROF_ADD_COUNTER(GP_FMIDX_LOOKUP_DIST,0);
            ++current_position;
          }
          if (current_position < num_candidate_text_positions) {
            batch[i].index_position = LF;
            batch[i].vector_rank = current_position;
            batch[i].distance = 0;
            batch[i].used_slot = true;
            ++current_position;
          }
        }
      }
    }
  }
  // Solve remaining queries
  for (i=0;i<DECODE_NUM_POSITIONS_PREFETCHED;++i) {
    if (batch[i].used_slot) {
      do {
        ++(batch[i].distance);
        batch[i].index_position = bwt_LF(bwt,batch[i].index_position,&is_sampled);
      } while (!is_sampled);
      filtering_position[batch[i].vector_rank].decode_sampled_pos = batch[i].index_position;
      filtering_position[batch[i].vector_rank].decode_distance = batch[i].distance;
      PROF_ADD_COUNTER(GP_FMIDX_LOOKUP_DIST,batch[i].distance);
    }
  }
  // Prefetch SA-retrieve samples
  const uint64_t bwt_length = fm_index_get_length(fm_index);
  uint64_t num_left_positions = num_candidate_text_positions;
  current_position = 0;
  while (num_left_positions > 0) {
    const uint64_t batch_size = MIN(num_left_positions,DECODE_NUM_POSITIONS_PREFETCHED);
    const uint64_t batch_top = current_position+batch_size;
    for (i=current_position;i<batch_top;++i) {
      sampled_sa_prefetch_sample(sampled_sa,filtering_position[i].decode_sampled_pos);
    }
    for (i=current_position;i<batch_top;++i) {
      filtering_position[i].region_text_position =
          (sampled_sa_get_sample(sampled_sa,filtering_position[i].decode_sampled_pos) +
              filtering_position[i].decode_distance) % bwt_length;
    }
    current_position = batch_top;
    num_left_positions -= batch_size;
  }
  // Adjust decoded position to the beginning of the read
  for (current_position=0;current_position<num_candidate_text_positions;++current_position) {
    // Locate Position
    filtering_position[current_position].locator_interval =
        locator_lookup_interval(locator,filtering_position[current_position].region_text_position);
    // Adjust Position
    region_search_t* const source_region = vector_get_elm(filtering_candidates->regions_buffer,
        filtering_position[current_position].source_region_offset,region_search_t);
    filtering_candidates_compute_text_coordinates(
        filtering_candidates,filtering_position+current_position,pattern,
        source_region->begin,pattern->key_length-source_region->begin);
  }
}
/*
 * Process Candidates
 */
uint64_t filtering_candidates_process_candidates(
    filtering_candidates_t* const filtering_candidates,
    pattern_t* const pattern,
    const bool keep_matching_regions) {
  // Check non-empty pending candidates set
  uint64_t pending_candidates = vector_get_used(filtering_candidates->filtering_positions);
  PROF_ADD_COUNTER(GP_CANDIDATE_POSITIONS,pending_candidates);
  if (pending_candidates==0) return 0; // Nothing to do
  // Batch decode+adjust of all positions of the candidates
  PROFILE_START(GP_FC_PROCESS_CANDIDATES,PROFILE_LEVEL);
  PROFILE_START(GP_FC_DECODE_POSITIONS,PROFILE_LEVEL);
  const uint64_t key_length = pattern->key_length;
  if (pending_candidates < DECODE_NUM_POSITIONS_PREFETCHED) {
    filtering_candidates_decode_filtering_positions(filtering_candidates,pattern);
  } else {
    filtering_candidates_decode_filtering_position_batch_prefetched(filtering_candidates,pattern);
  }
  PROFILE_STOP(GP_FC_DECODE_POSITIONS,PROFILE_LEVEL);
  // Compose matching regions into candidate regions (also filter out duplicated positions or already checked)
  PROFILE_START(GP_FC_COMPOSE_REGIONS,PROFILE_LEVEL);
  search_parameters_t* const search_parameters = filtering_candidates->search_parameters;
  const bool compose_region_chaining = keep_matching_regions && search_parameters->alignment_scaffolding;
  pending_candidates = filtering_candidates_compose_filtering_regions(
      filtering_candidates,key_length,pattern->max_effective_bandwidth,compose_region_chaining);
  PROFILE_STOP(GP_FC_COMPOSE_REGIONS,PROFILE_LEVEL);
  PROF_ADD_COUNTER(GP_CANDIDATE_REGIONS,pending_candidates);
  // Return total candidate regions
  PROFILE_STOP(GP_FC_PROCESS_CANDIDATES,PROFILE_LEVEL);
  return pending_candidates;
}
