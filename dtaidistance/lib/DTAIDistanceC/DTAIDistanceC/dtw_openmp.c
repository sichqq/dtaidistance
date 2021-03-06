/*!
@file dtw_openmp.c
@brief DTAIDistance.dtw

@author Wannes Meert
@copyright Copyright © 2020 Wannes Meert. Apache License, Version 2.0, see LICENSE for details.
*/

#include "dtw_openmp.h"

// TODO: Check if the _OPENMP macro is set

/**
 Check the arguments passed to dtw_distances_* and prepare the array of indices to be used.
 The indices are created upfront to allow for easy parallelization.
 
 @param block Block to indicate which series to compare.
 @param nb_series Number of series
 @param cbs Column begin indices per row
 @param rls Location start for row in distances array
 @param length Length of (compact) distances matrix
 @param settings : Settings for DTW
 
 @return 0 if all is ok, other number if not.
 */
int dtw_distances_prepare(DTWBlock *block, size_t nb_series, size_t **cbs, size_t **rls, size_t *length, DTWSettings *settings) {
    size_t cb, rs, ir;
    
    *length = dtw_distances_length(block, nb_series, settings->use_ssize_t);
    if (length == 0) {
        return 1;
    }
    
    // Correct block
    if (block->re == 0) {
        block->re = nb_series;
    }
    if (block->ce == 0) {
        block->ce = nb_series;
    }
    if (block->re <= block->rb) {
        *length = 0;
        return 1;
    }
    if (block->ce <= block->cb) {
        *length = 0;
        return 1;
    }

    *cbs = (size_t *)malloc(sizeof(size_t) * (block->ce - block->cb));
    if (!cbs) {
        printf("Error: dtw_distances_* - cannot allocate memory (cbs length = %zu)", block->ce - block->cb);
        *length = 0;
        return 1;
    }
    *rls = (size_t *)malloc(sizeof(size_t) * (block->ce - block->cb));
    if (!rls) {
        printf("Error: dtw_distances_* - cannot allocate memory (rls length = %zu)", block->ce - block->cb);
        *length = 0;
        return 1;
    }
    ir = 0;
    rs = 0;
    assert(block->rb < block->re);
    for (size_t r=block->rb; r<block->re; r++) {
        if (r + 1 > block->cb) {
            cb = r+1;
        } else {
            cb = block->cb;
        }
        (*cbs)[ir] = cb;
        (*rls)[ir] = rs;
        rs += block->ce - cb;
        ir += 1;
    }
    return 0;
}


/*!
Distance matrix for n-dimensional DTW, executed on a list of pointers to arrays and in parallel.

@see dtw_distances_ptrs
*/
size_t dtw_distances_ptrs_parallel(seq_t **ptrs, size_t nb_ptrs, size_t* lengths, seq_t* output,
                     DTWBlock* block, DTWSettings* settings) {
    // Requires openmp which is not supported for clang on mac by default (use newer version of clang)
    size_t r, c, r_i, c_i;
    size_t length;
    size_t *cbs, *rls;

    if (dtw_distances_prepare(block, nb_ptrs, &cbs, &rls, &length, settings) != 0) {
        return 0;
    }

    r_i=0;
    // Rows have different lengths, thus use guided scheduling to make threads with shorter rows
    // not wait for threads with longer rows. Also the first rows are always longer than the last
    // ones (upper triangular matrix), so this nicely aligns with the guided strategy.
    // Using schedule("static, 1") is also fast for the same reason (neighbor rows are almost
    // the same length, thus a circular assignment works well) but assumes all DTW computations take
    // the same amount of time.
    #pragma omp parallel for private(r_i, c_i, r, c) schedule(guided)
    for (r_i=0; r_i < (block->re - block->rb); r_i++) {
        r = block->rb + r_i;
        c_i = 0;
        for (c=cbs[r_i]; c<block->ce; c++) {
            // printf("r_i=%zu - r=%zu - c_i=%zu - c=%zu\n", r_i, r, c_i, c);
            double value = dtw_distance(ptrs[r], lengths[r],
                                        ptrs[c], lengths[c], settings);
            // printf("r_i=%zu - r=%zu - c_i=%zu - c=%zu - value=%.4f\n", r_i, r, c_i, c, value);
            output[rls[r_i] + c_i] = value;
            c_i++;
        }
    }
    
    free(cbs);
    free(rls);
    return length;
}


/*!
 Distance matrix for n-dimensional DTW, executed on a list of pointers to arrays and in parallel.
 
@see dtw_distances_ndim_ptrs
 */
size_t dtw_distances_ndim_ptrs_parallel(seq_t **ptrs, size_t nb_ptrs, size_t* lengths, int ndim, seq_t* output,
                                        DTWBlock* block, DTWSettings* settings) {
    // Requires openmp which is not supported for clang on mac by default (use newer version of clang)
    size_t r, c, r_i, c_i;
    size_t length;
    size_t *cbs, *rls;

    if (dtw_distances_prepare(block, nb_ptrs, &cbs, &rls, &length, settings) != 0) {
       return 0;
   }

   r_i=0;
   #pragma omp parallel for private(r_i, c_i, r, c) schedule(guided)
   for (r_i=0; r_i < (block->re - block->rb); r_i++) {
        r = block->rb + r_i;
        c_i = 0;
        for (c=cbs[r_i]; c<block->ce; c++) {
           double value = dtw_distance_ndim(ptrs[r], lengths[r],
                          ptrs[c], lengths[c],
                          ndim, settings);
           //        printf("pi=%zu - r=%zu - c=%zu - value=%.4f\n", pi, r, c, value);
           output[rls[r_i] + c_i] = value;
           c_i++;
        }
    }
   
    free(cbs);
    free(rls);
    return length;
}


/*!
 Distance matrix for n-dimensional DTW, executed on a 2-dimensional array and in parallel.
  
@see dtw_distances_matrix
 */
size_t dtw_distances_matrix_parallel(seq_t *matrix, size_t nb_rows, size_t nb_cols, seq_t* output, DTWBlock* block, DTWSettings* settings) {
    // Requires openmp which is not supported for clang on mac by default (use newer version of clang)
    size_t r, c, r_i, c_i;
    size_t length;
    size_t *cbs, *rls;

    if (dtw_distances_prepare(block, nb_rows, &cbs, &rls, &length, settings) != 0) {
        return 0;
    }
    
    r_i = 0;
    #pragma omp parallel for private(r_i, c_i, r, c) schedule(guided)
    for (r_i=0; r_i < (block->re - block->rb); r_i++) {
         r = block->rb + r_i;
         c_i = 0;
         for (c=cbs[r_i]; c<block->ce; c++) {
             double value = dtw_distance(&matrix[r*nb_cols], nb_cols,
                                         &matrix[c*nb_cols], nb_cols, settings);
             output[rls[r_i] + c_i] = value;
             c_i++;
         }
    }
    
    free(cbs);
    free(rls);
    return length;
}


/*!
Distance matrix for n-dimensional DTW, executed on a 3-dimensional array and in parallel.
 
@see dtw_distances_ndim_matrix
*/
size_t dtw_distances_ndim_matrix_parallel(seq_t *matrix, size_t nb_rows, size_t nb_cols, int ndim, seq_t* output, DTWBlock* block, DTWSettings* settings) {
    // Requires openmp which is not supported for clang on mac by default (use newer version of clang)
    size_t r, c, r_i, c_i;
    size_t length;
    size_t *cbs, *rls;

    if (dtw_distances_prepare(block, nb_rows, &cbs, &rls, &length, settings) != 0) {
        return 0;
    }

    r_i = 0;
    #pragma omp parallel for private(r_i, c_i, r, c) schedule(guided)
    for (r_i=0; r_i < (block->re - block->rb); r_i++) {
         r = block->rb + r_i;
         c_i = 0;
         for (c=cbs[r_i]; c<block->ce; c++) {
             double value = dtw_distance_ndim(&matrix[r*nb_cols*ndim], nb_cols,
                                                      &matrix[c*nb_cols*ndim], nb_cols,
                                                      ndim, settings);
             //        printf("pi=%zu - r=%zu->%zu - c=%zu - value=%.4f\n", pi, r, r*nb_cols, c, value);
            output[rls[r_i] + c_i] = value;
            c_i++;
         }
    }
    
    free(cbs);
    free(rls);
    return length;
}
