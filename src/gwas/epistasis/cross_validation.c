#include "cross_validation.h"


int** get_k_folds(unsigned int num_samples_affected, unsigned int num_samples_unaffected, unsigned int k, unsigned int **sizes) {
    if (num_samples_affected < k) {
        LOG_WARN("There are less affected samples than folds and they won't be properly distributed");
    }
    if (num_samples_unaffected < k) {
        LOG_WARN("There are less unaffected samples than folds and they won't be properly distributed");
    }
    
    // Fill an array of samples identifiers
    unsigned int num_samples = num_samples_affected + num_samples_unaffected;
    int samples[num_samples];
    for (int i = 0; i < num_samples; i++) {
        samples[i] = i;
    }
    
    // Shuffle affected and unaffected samples separately in order to guarantee they are not mixed
    array_shuffle_int(samples, num_samples_affected);
    array_shuffle_int(samples + num_samples_affected, num_samples_unaffected);
    
    // Get size of each fold (total, affected, unaffected) and initialize them
    int **folds = malloc (k * sizeof(unsigned int*));
    unsigned int *fold_sizes = calloc(3 * k, sizeof(unsigned int));
    unsigned int fold_size = num_samples / k;
    unsigned int mod_folds_size = num_samples % k;
    unsigned int all_folds_size = fold_size * k;
    for (int i = 0; i < k; i++) {
        // If the fold size can't be exactly the same for all folds, make distribution a bit uneven
        fold_sizes[3 * i] = (i < mod_folds_size) ? fold_size + 1 : fold_size ;
    }
    
    // Temporary buffers for storing affected and unaffected samples,
    // that will be merged so they are properly grouped for counting
    int total_affected_assigned = 0, total_unaffected_assigned = 0;
    int *samples_affected[k];
    int *samples_unaffected[k];
    for (int i = 0; i < k; i++) {
        samples_affected[i] = malloc(fold_sizes[3 * i] * sizeof(int));
        samples_unaffected[i] = malloc(fold_sizes[3 * i] * sizeof(int));
    }
    
    // Fill k-folds array
    // While the number of assigned samples is less than the total, assign one to each fold
    int i = 0;
    while (total_affected_assigned + total_unaffected_assigned < num_samples) {
        for (i = 0; i < k && total_affected_assigned + total_unaffected_assigned < num_samples; i++) {
            int my_offset_aff = fold_sizes[3 * i + 1];
            int my_offset_unaff = fold_sizes[3 * i + 2];
            
            if (total_affected_assigned < num_samples_affected) {
                samples_affected[i][my_offset_aff] = samples[total_affected_assigned];
                (fold_sizes[3 * i + 1])++ ; // Number of affected in a fold
                total_affected_assigned++ ;
            }
            
            if (total_unaffected_assigned < num_samples_unaffected) {
                samples_unaffected[i][my_offset_unaff] = samples[num_samples_affected + total_unaffected_assigned];
                (fold_sizes[3 * i + 2])++ ; // Number of unaffected in a fold
                total_unaffected_assigned++ ;
            }
        }
        
        LOG_DEBUG_F("total [%u] affected assigned = %u\tunaffected assigned = %u\n", i, total_affected_assigned, total_unaffected_assigned);
        
        i = (i < k) ? i + 1 : 0;
    }
    
    // Adjust to true values, compacting the array and removing -1 values
    for (int i = 0; i < k; i++) {
        fold_sizes[3 * i] = fold_sizes[3 * i + 1] + fold_sizes[3 * i + 2];
        folds[i] = malloc(fold_sizes[3 * i] * sizeof(int));
        
        // Insert affected samples
        memcpy(folds[i], samples_affected[i], fold_sizes[3 * i + 1] * sizeof(int));
        // Insert unaffected samples
        memcpy(folds[i] + fold_sizes[3 * i + 1], samples_unaffected[i], fold_sizes[3 * i + 2] * sizeof(int));
        // Sort them
        qsort(folds[i], fold_sizes[3 * i], sizeof(int), compare_int);
    }
    
    // Free temporary data
    for (int i = 0; i < k; i++) {
        free(samples_affected[i]);
        free(samples_unaffected[i]);
    }
    
//     printf("FOLDS:\n");
//     for (int i = 0; i < k; i++) {
//         printf("[%d] size (%d,%d,%d) -> ", i, fold_sizes[3 * i], fold_sizes[3 * i + 1], fold_sizes[3 * i + 2]);
//         
//         for (int j = 0; j < fold_sizes[3 * i]; j++) {
//             printf("%d ", folds[i][j]);
//         }
//         printf("\n");
//     }
    
    *sizes = fold_sizes;
    return folds;
}


uint8_t *get_genotypes_for_combination_and_fold(int order, int comb[order], int num_samples, 
                                                int num_samples_in_fold, int fold_samples[num_samples_in_fold], 
                                                int stride, uint8_t **block_starts) {
    uint8_t *genotypes = malloc (order * num_samples_in_fold * sizeof(uint8_t));
    
    LOG_DEBUG_F("start 0 = %d\tstart 1 = %d\n", *block_starts[0], *block_starts[1]);
    
    int snp_index[order];
    for (int i = 0; i < order; i++) {
        // Get 'row' inside block
        snp_index[i] = comb[i] % stride;
        LOG_DEBUG_F("snp index %d = %d\t * num_samples = %d\n", i, snp_index[i], snp_index[i] * num_samples);
        // Get matrix index (a row contains a certain number of samples)
        snp_index[i] *= num_samples;
        
        // Get genotypes by adding the offset of the samples in the fold
        for (int j = 0; j < num_samples_in_fold; j++) {
            LOG_DEBUG_F("offset = %d\tcontents = %d\n", snp_index[i] + fold_samples[j], *(block_starts[i] + snp_index[i] + fold_samples[j]));
            genotypes[i * num_samples_in_fold + j] = *(block_starts[i] + snp_index[i] + fold_samples[j]);
        }
    }
    
    return genotypes;
}

uint8_t *get_genotypes_for_block_exclude_fold(int num_variants, int num_samples, int num_samples_in_fold, int fold_samples[num_samples_in_fold], 
                                              int stride, int block_coord, uint8_t *block_start) {
//     uint8_t *genotypes = malloc (order * (num_samples - num_samples_in_fold) * sizeof(uint8_t));
//     uint8_t *genotypes = malloc (MIN(stride, num_variants - block_coord) * (num_samples - num_samples_in_fold) * sizeof(uint8_t));   // TODO complicates transposition
    uint8_t *genotypes = malloc (stride * (num_samples - num_samples_in_fold) * sizeof(uint8_t));
    int genotypes_copied = 0;
    int subset_length = 0;
    
//     LOG_DEBUG_F("start 0 = %d\tstart 1 = %d\n", *block_starts[0], *block_starts[1]);
    
//     int snp_index[order];
    for (int i = 0; i < stride && block_coord + i < num_variants; i++) {
        // Get 'row' inside block
//         snp_index = comb[i] % stride;
        int snp_index = i * num_samples;
        LOG_DEBUG_F("snp index %d = %d\t by num_samples = %d\n", i, snp_index, snp_index * num_samples);
        // Get matrix index (a row contains a certain number of samples)
        
        // ------- Copy all samples NOT included in that fold ---------
        
        subset_length = 0;
        
        // Copy from first sample to first in the fold
        if (fold_samples[0] > 0) {
            LOG_DEBUG_F("[%d] copy from %d to %d\n", i, 0, fold_samples[0]);
            memcpy(genotypes + genotypes_copied, block_start + snp_index, fold_samples[0] * sizeof(uint8_t));
            genotypes_copied += fold_samples[0];
        }
        
        // Copy contents between each pair of samples in the fold
        for (int j = 1; j < num_samples_in_fold; j++) {
            subset_length = fold_samples[j] - (fold_samples[j-1] + 1);
            // For example, if the current subset ends in index 4 
            // and the next one starts in 5, nothing is copied
            if (subset_length > 0) {
                LOG_DEBUG_F("[%d] copy from %d to %d\n", i, fold_samples[j-1] + 1, fold_samples[j]);
                memcpy(genotypes + genotypes_copied, block_start + snp_index + fold_samples[j-1] + 1, subset_length * sizeof(uint8_t));
                genotypes_copied += subset_length;
            }
        }
        
        // Copy from the last sample in the fold to the last one in the whole dataset
        subset_length = num_samples - (fold_samples[num_samples_in_fold - 1] + 1);
        if (subset_length > 0) {
            LOG_DEBUG_F("[%d] copy from %d to %d\n", i, fold_samples[num_samples_in_fold - 1] + 1, num_samples);
            memcpy(genotypes + genotypes_copied, block_start + snp_index + fold_samples[num_samples_in_fold - 1] + 1, subset_length * sizeof(uint8_t));
            genotypes_copied += subset_length;
        }
        
        LOG_DEBUG_F("[%d] genotypes copied = %d\n\n", i, genotypes_copied);
        assert(genotypes_copied == (num_samples - num_samples_in_fold) * (i+1));
    }
    
//     printf("copied = { ");
//     for (int k = 0; k < 20; k++) {
//         printf("%d ", genotypes[k]);
//     }
//     printf("... }\n");
    return genotypes;
}

uint8_t *get_genotypes_for_combination_exclude_fold(int order, int comb[order], int num_samples, 
                                                    int num_samples_in_fold, int fold_samples[num_samples_in_fold], 
                                                    int stride, uint8_t **block_starts) {
    uint8_t *genotypes = malloc (order * (num_samples - num_samples_in_fold) * sizeof(uint8_t));
    int genotypes_copied = 0;
    int subset_length = 0;
    
    LOG_DEBUG_F("start 0 = %d\tstart 1 = %d\n", *block_starts[0], *block_starts[1]);
    
    int snp_index[order];
    for (int i = 0; i < order; i++) {
        // Get 'row' inside block
        snp_index[i] = comb[i] % stride;
        LOG_DEBUG_F("snp index %d = %d\t * num_samples = %d\n", i, snp_index[i], snp_index[i] * num_samples);
        // Get matrix index (a row contains a certain number of samples)
        snp_index[i] *= num_samples;
        
        // ------- Copy all samples NOT included in that fold ---------
        
        subset_length = 0;
        
        // Copy from first sample to first in the fold
        if (fold_samples[0] > 0) {
            LOG_DEBUG_F("[%d] copy from %d to %d\n", i, 0, fold_samples[0]);
            memcpy(genotypes + genotypes_copied, block_starts[i] + snp_index[i], fold_samples[0] * sizeof(uint8_t));
            genotypes_copied += fold_samples[0];
        }
        
        // Copy contents between each pair of samples in the fold
        for (int j = 1; j < num_samples_in_fold; j++) {
            subset_length = fold_samples[j] - (fold_samples[j-1] + 1);
            // For example, if the current subset ends in index 4 
            // and the next one starts in 5, nothing is copied
            if (subset_length > 0) {
                LOG_DEBUG_F("[%d] copy from %d to %d\n", i, fold_samples[j-1] + 1, fold_samples[j]);
                memcpy(genotypes + genotypes_copied, block_starts[i] + snp_index[i] + fold_samples[j-1] + 1, subset_length * sizeof(uint8_t));
                genotypes_copied += subset_length;
            }
        }
        
        // Copy from the last sample in the fold to the last one in the whole dataset
        subset_length = num_samples - (fold_samples[num_samples_in_fold - 1] + 1);
        if (subset_length > 0) {
            LOG_DEBUG_F("[%d] copy from %d to %d\n", i, fold_samples[num_samples_in_fold - 1] + 1, num_samples);
            memcpy(genotypes + genotypes_copied, block_starts[i] + snp_index[i] + fold_samples[num_samples_in_fold - 1] + 1, subset_length * sizeof(uint8_t));
            genotypes_copied += subset_length;
        }
        
        LOG_DEBUG_F("[%d] genotypes copied = %d\n\n", i, genotypes_copied);
        assert(genotypes_copied == (num_samples - num_samples_in_fold) * (i+1));
    }
    
    return genotypes;
}
