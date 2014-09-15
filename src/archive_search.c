/*
 * PROJECT: GEMMapper
 * FILE: archive_search.c
 * DATE: 06/06/2012
 * AUTHOR(S): Santiago Marco-Sola <santiagomsola@gmail.com>
 */

#include "archive_search.h"

/*
 * Archive Search Setup
 */
GEM_INLINE archive_search_t* archive_search_new(
    archive_t* const archive,search_parameters_t* const search_parameters,
    select_parameters_t* const select_parameters) {
  ARCHIVE_CHECK(archive);
  // Allocate handler
  archive_search_t* const archive_search = mm_alloc(archive_search_t);
  // Archive
  archive_search->archive = archive;
  // Sequence
  sequence_init(&archive_search->sequence);
  sequence_init(&archive_search->rc_sequence);
  // Approximate Search
  archive_search->search_actual_parameters.search_parameters = search_parameters;
  archive_search->select_parameters = select_parameters;
  approximate_search_init(
      &archive_search->forward_search_state,
      archive->locator,archive->graph,archive->enc_text,archive->fm_index,
      &archive_search->search_actual_parameters);
  approximate_search_init(
      &archive_search->reverse_search_state,
      archive->locator,archive->graph,archive->enc_text,archive->fm_index,
      &archive_search->search_actual_parameters);
  // Archive search control (Flow control) [DEFAULTS]
  archive_search->probe_strand = true;
  archive_search->search_reverse = !archive->indexed_complement;
  // Return
  return archive_search;
}
GEM_INLINE void archive_search_configure(
    archive_search_t* const archive_search,mm_search_t* const mm_search) {
  // Text-Collection
  archive_search->text_collection = &mm_search->text_collection;
  // Clear F/R search states
  approximate_search_configure(
      &archive_search->forward_search_state,&mm_search->text_collection,
      &mm_search->filtering_candidates_forward,&mm_search->interval_set,
      mm_search->mm_stack);
  approximate_search_configure(
      &archive_search->reverse_search_state,&mm_search->text_collection,
      &mm_search->filtering_candidates_reverse,&mm_search->interval_set,
      mm_search->mm_stack);
  // MM
  archive_search->mm_stack = mm_search->mm_stack;
}
GEM_INLINE void archive_search_prepare_sequence(archive_search_t* const archive_search) {
  // Check the index characteristics & generate reverse-complement (if needed)
  if (archive_search->archive->indexed_complement) {
    archive_search->search_reverse = false;
  } else {
    if (archive_search->archive->filter_type == Iupac_colorspace_dna) {
      sequence_generate_reverse_complement(&archive_search->sequence,&archive_search->rc_sequence);
    } else {
      sequence_generate_reverse(&archive_search->sequence,&archive_search->rc_sequence);
    }
    archive_search->search_reverse = !sequence_equals(&archive_search->sequence,&archive_search->rc_sequence);
  }
  // Generate the pattern(s)
  approximate_search_prepare_pattern(
      &archive_search->forward_search_state,&archive_search->sequence);
  if (archive_search->search_reverse) {
    approximate_search_prepare_pattern(
        &archive_search->reverse_search_state,&archive_search->rc_sequence);
  }
}
GEM_INLINE void archive_search_reset(archive_search_t* const archive_search,const uint64_t sequence_length) {
  // Instantiate parameters actual-values
  approximate_search_instantiate_values(&archive_search->search_actual_parameters,sequence_length);
  // Clear F/R search states
  approximate_search_reset(&archive_search->forward_search_state);
  approximate_search_reset(&archive_search->reverse_search_state);
  // Prepare for sequence
  archive_search_prepare_sequence(archive_search);
}
GEM_INLINE void archive_search_delete(archive_search_t* const archive_search) {
  // Delete Sequence
  sequence_destroy(&archive_search->sequence);
  sequence_destroy(&archive_search->rc_sequence);
  // Destroy search states
  approximate_search_destroy(&archive_search->forward_search_state);
  approximate_search_destroy(&archive_search->reverse_search_state);
  // Free handler
  mm_free(archive_search);
}
/*
 * Archive Search [Accessors]
 */
GEM_INLINE sequence_t* archive_search_get_sequence(const archive_search_t* const archive_search) {
  return (sequence_t*)&archive_search->sequence;
}
GEM_INLINE uint64_t archive_search_get_num_potential_canditates(const archive_search_t* const archive_search) {
  if (archive_search->archive->indexed_complement) {
    return archive_search->forward_search_state.num_potential_candidates;
  } else {
    return archive_search->forward_search_state.num_potential_candidates +
           archive_search->reverse_search_state.num_potential_candidates;
  }
}
/*
 * SingleEnd Indexed Search (SE Online Approximate String Search)
 */
GEM_INLINE void archive_search_single_end(archive_search_t* const archive_search,matches_t* const matches) {
  ARCHIVE_SEARCH_CHECK(archive_search);
  // Reset initial values (Prepare pattern(s), instantiate parameters values, ...)
  archive_search_reset(archive_search,sequence_get_length(&archive_search->sequence));
  // Search the pattern(s)
  approximate_search_t* const forward_asearch = &archive_search->forward_search_state;
  if (!archive_search->search_reverse) {
    // Compute the full search
    forward_asearch->stop_search_stage = asearch_end; // Don't stop until search is done
    forward_asearch->search_strand = Forward;
    approximate_search(forward_asearch,matches);
  } else {
    // Configure search stage to stop at
    forward_asearch->stop_search_stage =
        (archive_search->search_actual_parameters.complete_strata_after_best_nominal < forward_asearch->max_differences
            && archive_search->probe_strand) ? asearch_neighborhood : asearch_end;
    // Run the search (FORWARD)
    forward_asearch->search_strand = Forward; // Configure forward search
    approximate_search(forward_asearch,matches);
    // Check the number of matches & keep searching
    if (!forward_asearch->max_matches_reached) {
      // Keep on searching
      approximate_search_t* const reverse_asearch = &archive_search->reverse_search_state;
      reverse_asearch->stop_search_stage = asearch_end; // Force a full search
      // Run the search (REVERSE)
      reverse_asearch->search_strand = Reverse; // Configure reverse search
      approximate_search(reverse_asearch,matches);
      // Resume forward search (if not completed before)
      if (forward_asearch->current_search_stage != asearch_end && !forward_asearch->max_matches_reached) {
        forward_asearch->stop_search_stage = asearch_end;
        forward_asearch->search_strand = Forward; // Configure forward search
        approximate_search(forward_asearch,matches);
      }
    }
  }
//  #ifdef GEM_MAPPER_DEBUG
//   const uint64_t check_level = search_params->internal_parameters.check_alignments;
//  extern bool keepon;
//  if (keepon && (check_level&CHECK_ALG_P_CORRECTNESS)) { // Check p-correctness
//     const uint64_t old_misms = search_params->max_mismatches;
//    search_params->max_mismatches = caller_max_misms; // Full check
//    fmi_check_pos_correctness(archive->index,search_params,matches,false,mpool);
//    search_params->max_mismatches = old_misms;
//  }
//  if (keepon && (check_level&CHECK_ALG_I_CORRECTNESS)) { // Check i-correctness
//     const uint64_t old_misms = search_params->max_mismatches;
//    search_params->max_mismatches = caller_max_misms; // Full check
//    fmi_check_int_correctness(archive->index,search_params,matches,false,mpool);
//    search_params->max_mismatches = old_misms;
//  }
//  if (keepon && (check_level&CHECK_ALG_COMPLETENESS) && search_params->num_wildcards==0) { // Check completeness
//     const uint64_t old_misms = search_params->max_mismatches;
//    search_params->max_mismatches = (search_params->fast_mapping_degree>0) ?
//        matches->max_complete_stratum-1 : old_misms; /*caller_max_misms*/ // Full check
//    fmi_check_completeness(archive->index,search_params,matches,false,mpool);
//    search_params->max_mismatches = old_misms;
//  }
//  #endif
}