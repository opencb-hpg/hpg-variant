/*
 * Copyright (c) 2012-2013 Cristina Yenyxe Gonzalez Garcia (ICM-CIPF)
 * Copyright (c) 2012 Ignacio Medina (ICM-CIPF)
 *
 * This file is part of hpg-variant.
 *
 * hpg-variant is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * hpg-variant is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with hpg-variant. If not, see <http://www.gnu.org/licenses/>.
 */

#include "annot.h"

static void vcf_annot_parse_effect_response(int tid, vcf_record_t ** variants, int num_variants);
static void vcf_annot_process_dbsnp(char *chr, long pos, char * id, array_list_t *array_effect);
static void vcf_annot_parse_snp_response(int tid, vcf_record_t ** variants, int num_variants);
static void print_samples(array_list_t* sample_list);
int invoke_snp_ws(const char *url, vcf_record_t **records, int num_records);
static size_t save_snp_response(char *contents, size_t size, size_t nmemb, void *userdata);
int set_field_value_in_info(char *key, char *value, bool append, vcf_record_t *record);

typedef struct vcf_annot_effect{
    char *chr;
    long pos;
    char * ids;
} vcf_annot_effect_t;

typedef struct vcf_annot_dbsnp{
    char* chr;
    long pos;
    char *id;
} vcf_annot_dbsnp_t;

void vcf_annot_effect_free(vcf_annot_effect_t *effect) {
    if(effect != NULL) {
        free(effect->ids);
        free(effect->chr);
        free(effect);
    }
}

void vcf_annot_dbsnp_free(vcf_annot_dbsnp_t *dbsnp) {
    if(dbsnp != NULL) {
        free(dbsnp->id);
        free(dbsnp->chr);
        free(dbsnp);
    }
}

int run_annot(char **urls, shared_options_data_t *shared_options_data, annot_options_data_t *options_data) {

    int ret_code;
    double start, stop, total;
    vcf_annot_sample_t *annot_sample;
    vcf_annot_chr_t *annot_chr;
    vcf_annot_pos_t *annot_pos;
    vcf_annot_bam_t *annot_bam;
    char *sample_name;
    char * copy_buf;
    char *directory;
    khiter_t iter;
    int ret;
    list_item_t *output_item;

    // Check the Trailling slash
    if(options_data->bam_directory[strlen(options_data->bam_directory) - 1] != '/') {
        directory = (char*) calloc(strlen(options_data->bam_directory) + 2, sizeof(char));
        strcpy(directory, options_data->bam_directory);
        strcat(directory, "/");
    }else{
        directory = (char*) calloc(strlen(options_data->bam_directory) + 1, sizeof(char));
        strcpy(directory, options_data->bam_directory);
    }

    vcf_file_t *vcf_file = vcf_open(shared_options_data->vcf_filename, shared_options_data->max_batches);
    if (!vcf_file) {
        LOG_FATAL("VCF file does not exist!\n");
    }
    
    ret_code = create_directory(shared_options_data->output_directory);
    if (ret_code != 0 && errno != EEXIST) {
        LOG_FATAL_F("Can't create output directory: %s\n", shared_options_data->output_directory);
    }

    // Initialize environment for connecting to the web service
    ret_code = init_http_environment(0);
    if (ret_code != 0) {
        return ret_code;
    }
    
    initialize_ws_buffers(shared_options_data->num_threads);

    LOG_INFO("Annotating VCF file...");
    
    array_list_t *sample_list = array_list_new(100, 1.25f, COLLECTION_MODE_SYNCHRONIZED);
    
    list_t *output_list = malloc(sizeof(list_t));
    list_init("output_list", shared_options_data->num_threads, INT_MAX, output_list);

#pragma omp parallel sections private(start, stop, total)
    {
#pragma omp section
        {
            LOG_DEBUG_F("Thread %d reads the VCF file\n", omp_get_thread_num());
            // Reading
            start = omp_get_wtime();

            if (shared_options_data->batch_bytes > 0) {
                ret_code = vcf_parse_batches_in_bytes(shared_options_data->batch_bytes, vcf_file);
            } else if (shared_options_data->batch_lines > 0) {
                ret_code = vcf_parse_batches(shared_options_data->batch_lines, vcf_file);
            }

            stop = omp_get_wtime();
            total = stop - start;

            if (ret_code) { LOG_FATAL_F("[%dR] Error code = %d\n", omp_get_thread_num(), ret_code); }

            LOG_INFO_F("[%dR] Time elapsed = %f s\n", omp_get_thread_num(), total);
            LOG_INFO_F("[%dR] Time elapsed = %e ms\n", omp_get_thread_num(), total*1000);

            notify_end_parsing(vcf_file);
        }

#pragma omp section
        {
            // Enable nested parallelism and set the number of threads the user has chosen
            omp_set_nested(1);
            LOG_INFO_F("Thread %d processes data\n", omp_get_thread_num());

            start = omp_get_wtime();

            int i = 0;

            vcf_batch_t *batch = NULL;
            khash_t(bams) *sample_bams = kh_init(bams);
            while ((batch = fetch_vcf_batch(vcf_file)) != NULL) {
                if(i == 0 && options_data->missing > 0) {
                    for (int n = 0; n < array_list_size(vcf_file->samples_names); n++) {
                        sample_name = (char*) array_list_get(n, vcf_file->samples_names);
                        iter = kh_get(ids, sample_bams, sample_name);

                        if (iter != kh_end(sample_bams)) {
                            LOG_FATAL_F("Sample %s appears more than once. File can not be analyzed.\n", annot_sample->name);
                        } else {
                            iter = kh_put(bams, sample_bams, sample_name, &ret);

                            if (ret) {
                                annot_bam = (vcf_annot_bam_t*) malloc(sizeof(vcf_annot_bam_t));
                                annot_bam->bam_filename = (char*) calloc(strlen(directory) + strlen(sample_name) + 4 + 1, sizeof(char));
                                strcpy(annot_bam->bam_filename, directory);
                                strcat(annot_bam->bam_filename, sample_name);
                                strcat(annot_bam->bam_filename, ".bam");
                                
                                if(!exists(annot_bam->bam_filename)) {
                                    LOG_FATAL_F("File %s does not exist\n", annot_bam->bam_filename);
                                }

                                annot_bam->bai_filename = (char*) calloc(strlen(directory) + strlen(sample_name) + 8 + 1, sizeof(char));
                                strcpy(annot_bam->bai_filename, directory);
                                strcat(annot_bam->bai_filename, sample_name);
                                strcat(annot_bam->bai_filename, ".bam.bai");
                                
                                if(!exists(annot_bam->bai_filename)) {
                                    LOG_FATAL_F("File %s does not exist\n", annot_bam->bai_filename);
                                }

                                kh_value(sample_bams, iter) = annot_bam; 
                            }
                        }
                    }
                }

                if(i % 50 == 0) {
                    LOG_INFO_F("Batch %d reached by thread %d - %zu/%zu records \n", 
                            i, omp_get_thread_num(),
                            batch->records->size, batch->records->capacity);
                }
                
                for (int n = 0; n < array_list_size(vcf_file->samples_names); n++) {
                    annot_sample = (vcf_annot_sample_t*) malloc(sizeof(vcf_annot_sample_t));
                    sample_name = (char*) array_list_get(n, vcf_file->samples_names);
                    annot_sample->name = strndup(sample_name, strlen(sample_name));
                    annot_sample->chromosomes = array_list_new(24, 1.25f, COLLECTION_MODE_SYNCHRONIZED);
                    annot_sample->chromosomes->compare_fn = &vcf_annot_chr_cmp;
                    array_list_insert(annot_sample, sample_list);
                }

                // Divide the list of passed records in ranges of size defined in config file

                array_list_t *input_records = batch->records;
                bam_file_t* bam_file;
                bam_header_t *bam_header;
                bam_index_t *idx = 0;
                char* aux;
                char *bam_filename;
                int *chunk_sizes = NULL;
                int *chunk_starts;
                int result, tid, beg, end;
                int num_chunks;
                khiter_t iter;
                vcf_annot_chr_t *annot_chr;
                vcf_annot_pos_t *annot_pos;
                vcf_annot_sample_t *annot_sample;


                int reconnections = 0;
                int max_reconnections = 3; // TODO allow to configure?
                int ret_ws_0 = 0;
                int ret_ws_1 = 0;

                if(options_data->missing > 0){

                    // Maximum size processed by each thread (never allow more than 1000 variants per query)
                    if (shared_options_data->batch_lines > 0) {
                        shared_options_data->entries_per_thread = MIN(MAX_VARIANTS_PER_QUERY, 
                                ceil((float) shared_options_data->batch_lines / shared_options_data->num_threads));
                    } else {
                        shared_options_data->entries_per_thread = MAX_VARIANTS_PER_QUERY;
                    }
                    LOG_DEBUG_F("entries-per-thread = %d\n", shared_options_data->entries_per_thread);

                    chunk_starts = create_chunks(input_records->size, shared_options_data->entries_per_thread, &num_chunks, &chunk_sizes);
#pragma omp parallel for num_threads(shared_options_data->num_threads) 
                    for (int j = 0; j < num_chunks; j++) {
                        vcf_annot_process_chunk((vcf_record_t**)(input_records->items + chunk_starts[j]), chunk_sizes[j], sample_list, vcf_file);
                    }

#pragma omp parallel for num_threads(shared_options_data->num_threads) 
                    for (int j = 0; j < array_list_size(sample_list); j++) {
                        annot_sample = (vcf_annot_sample_t*) array_list_get(j, sample_list);
                        vcf_annot_sort_sample(annot_sample);
                    }

#pragma omp parallel for num_threads(shared_options_data->num_threads) 
                    for (int j = 0; j < array_list_size(sample_list); j++) {
                        annot_sample = (vcf_annot_sample_t*) array_list_get(j, sample_list);
                        vcf_annot_check_bams(annot_sample, sample_bams);
                    }
                    free(chunk_starts);
                    free(chunk_sizes);
                }
               

                if(options_data->dbsnp > 0 || options_data->effect >0){

                    shared_options_data->entries_per_thread = 100;
                    chunk_starts = create_chunks(input_records->size, shared_options_data->entries_per_thread, &num_chunks, &chunk_sizes);
                    
                    do {
#pragma omp parallel for num_threads(shared_options_data->num_threads)
                        for (int j = 0; j < num_chunks; j++) {
                            int tid = omp_get_thread_num();
                            if(options_data->effect && (!reconnections || ret_ws_0)) {

                                ret_ws_0 = invoke_effect_ws(urls[0], (vcf_record_t**) (input_records->items + chunk_starts[j]), chunk_sizes[j], NULL);
                                vcf_annot_parse_effect_response(tid, (vcf_record_t**) (input_records->items + chunk_starts[j]), chunk_sizes[j] );

                                free(effect_line[tid]);
                                effect_line[tid] = (char*) calloc (max_line_size[tid], sizeof(char));
                            }
                            if(options_data->dbsnp && (!reconnections || ret_ws_1)) {

                                ret_ws_0 = invoke_snp_ws(urls[1], (vcf_record_t**) (input_records->items + chunk_starts[j]), chunk_sizes[j]);
                                vcf_annot_parse_snp_response(tid, (vcf_record_t**) (input_records->items + chunk_starts[j]), chunk_sizes[j] );

                                free(snp_line[tid]);
                                snp_line[tid] = (char*) calloc (snp_max_line_size[tid], sizeof(char));
                            }
                        }
                        if(ret_ws_0 || ret_ws_1) {
                            if (ret_ws_0) {
                                LOG_ERROR_F("Effect web service error: %s\n", get_last_http_error(ret_ws_0));
                            }
                            if (ret_ws_1) {
                                LOG_ERROR_F("SNP web service error: %s\n", get_last_http_error(ret_ws_1));
                            }
                            reconnections++;
                            LOG_ERROR_F("Some errors ocurred, reconnection #%d\n", reconnections);
                            sleep(4);
                        } 
                    } while (reconnections < max_reconnections && (ret_ws_0 || ret_ws_1));
                    
                    free(chunk_starts);
                    free(chunk_sizes);
                }

                if (shared_options_data->batch_lines > 0) {
                    shared_options_data->entries_per_thread = MIN(MAX_VARIANTS_PER_QUERY, 
                            ceil((float) shared_options_data->batch_lines / shared_options_data->num_threads));
                } else {
                    shared_options_data->entries_per_thread = MAX_VARIANTS_PER_QUERY;
                }

                
                chunk_starts = create_chunks(input_records->size, shared_options_data->entries_per_thread, &num_chunks, &chunk_sizes);

#pragma omp parallel for num_threads(shared_options_data->num_threads) 
                for (int j = 0; j < num_chunks; j++) {
                    vcf_annot_edit_chunk((vcf_record_t**)(input_records->items + chunk_starts[j]), chunk_sizes[j], sample_list, sample_bams, vcf_file);
                }

                array_list_clear(sample_list, vcf_annot_sample_free);

                output_item = list_item_new(i, 0, batch);
                list_insert_item(output_item, output_list);

                free(chunk_starts);
                free(chunk_sizes);
                i++;
            } // End While

            stop = omp_get_wtime();
            total = stop - start;

            LOG_INFO_F("[%d] Time elapsed = %f s\n", omp_get_thread_num(), total);
            LOG_INFO_F("[%d] Time elapsed = %e ms\n", omp_get_thread_num(), total*1000);

            if (sample_bams) { kh_destroy(bams, sample_bams); }

            // Decrease list writers count
            for (int i = 0; i < shared_options_data->num_threads; i++) {
                list_decr_writers(output_list);
            }
        array_list_free(sample_list, vcf_annot_sample_free);
        } // End section

#pragma omp section
        {
            LOG_DEBUG_F("Thread %d writes the output\n", omp_get_thread_num());
            
            start = omp_get_wtime();

            char *output_filename = shared_options_data->output_filename;
            
            char *aux_filename;
            FILE *output_fd = get_output_file(shared_options_data, aux_filename, &aux_filename);
            LOG_INFO_F("Output filename = %s\n", aux_filename);
            
            list_item_t *list_item = NULL;
            vcf_header_entry_t *entry;
            vcf_record_t *record;
            int *num_records;
            vcf_batch_t *batch;
            int header_aux = 0;
            char *value;

            while( list_item = list_remove_item(output_list)) {
                if(header_aux == 0) { // Para asegurarnos que se ha leído toda la cabecera antes de escribirla
                    // Write the header
                    if(options_data->dbsnp > 0){
                        entry = vcf_header_entry_new();
                        set_vcf_header_entry_name("INFO", 4, entry);
                        value = "<ID=DB,Number=0,Type=Flag,Description=\"dbSNP id\">";
                        add_vcf_header_entry_value(value, strlen(value), entry);
                        add_vcf_header_entry(entry, vcf_file);
                    }
                    if(options_data->effect > 0){
                        entry = vcf_header_entry_new();
                        set_vcf_header_entry_name("INFO", 4, entry);
                        value = "<ID=EFF,Number=.,Type=String,Description=\"Effect\">";
                        add_vcf_header_entry_value(value, strlen(value), entry);
                        add_vcf_header_entry(entry, vcf_file);
                    }
                    write_vcf_header(vcf_file, output_fd);
                    header_aux = 1;
                }
                    batch = (vcf_batch_t*) list_item->data_p;
                    for (int i = 0; i < batch->records->size; i++) {
                        record = (vcf_record_t*) batch->records->items[i];
                        write_vcf_record(record, output_fd);
                    }
                    vcf_batch_free(batch);
                    list_item_free(list_item);
            }
            fclose(output_fd);
            free(output_filename);
            free(aux_filename);
        }
    } // End Parallel sections

    list_free_deep(output_list, vcf_batch_free);
    free(directory);

    vcf_close(vcf_file);
    return 0;
}


static void vcf_annot_sort_sample(vcf_annot_sample_t* annot_sample) {
    int j;
    vcf_annot_pos_t ** annot_pos_array;
    vcf_annot_chr_t *annot_chr = NULL;

    for (j = 0; j < array_list_size(annot_sample->chromosomes); j++) {
        annot_chr = (vcf_annot_chr_t*) array_list_get(j, annot_sample->chromosomes);
        annot_pos_array = (vcf_annot_pos_t*) annot_chr->positions->items;
        qsort(annot_pos_array, array_list_size(annot_chr->positions), sizeof(vcf_annot_pos_t), vcf_annot_pos_cmp);
    }
}

static int vcf_annot_pos_cmp (const void * a, const void * b) {
    long pos_a_val = (*(vcf_annot_pos_t **)a)->pos;  
    long pos_b_val = (*(vcf_annot_pos_t **)b)->pos; 

    return (pos_a_val - pos_b_val );
}

static void vcf_annot_parse_effect_response(int tid, vcf_record_t ** variants, int num_variants) { 
    char *aux_buffer;
    char *copy_buf;
    char *info;
    int count;
    int num_lines;
    vcf_record_t * record;
    char **split_batch = split(effect_line[tid], "\n", &num_lines);

    for (int i = 0, j = 0; i < num_lines; i++) {
        int num_columns;
        char *copy_buf = strdup(split_batch[i]);
        char **split_result = split(copy_buf, "\t", &num_columns);
        free(copy_buf);

        // Find consequence type name (always after SO field)
        if (num_columns == 25) {
            for(; j < num_variants; j++){
                record = variants[j];
                if(strncmp(split_result[0], record->chromosome, record->chromosome_len) == 0 && atoi(split_result[1]) == record->position){
                        set_field_value_in_info("EFF", split_result[19], true, record);
//                    info = strndup(record->info, record->info_len);
//                    if(strcmp(info, ".") == 0) {
//                        aux_buffer = calloc((strlen(split_result[19]) + strlen(info) + 8 + 1) , sizeof(char));
//                        strcpy(aux_buffer, "EFF=");
//                        strcat(aux_buffer, split_result[19]);
//                    }
//                    else{
//                        if(!strstr(info, split_result[19])){
//                            if(strstr(info, "EFF=")){
//                                aux_buffer = (char*) calloc((strlen(split_result[19]) + strlen(info) + 1 + 1), sizeof(char));
//                                strcpy(aux_buffer, info);
//                                strcat(aux_buffer, ",");
//                                strcat(aux_buffer, split_result[19]);
//                            }
//                            else{
//                                aux_buffer = calloc((strlen(split_result[19]) + strlen(info) + 8 + 1 + 1), sizeof(char));
//                                strcpy(aux_buffer, info);
//                                strcat(aux_buffer, ";");
//                                strcat(aux_buffer, "EFF=");
//                                strcat(aux_buffer, split_result[19]);
//                            }
//                        }
//                    }
//                    record->info = aux_buffer;
//                    record->info_len = strlen(aux_buffer);
//                    free(info);
                    break;
                }
            }
        } else {
            if (strlen(split_batch[i]) == 0) { // Last line in batch could be only a newline
                continue;
            }
            LOG_INFO_F("[%d] Non-valid line found (%d fields): '%s'\n", tid, num_columns, split_batch[i]);
        }

        for (int s = 0; s < num_columns; s++) {
            free(split_result[s]);
        }
        free(split_result);

    }

    for (int i = 0; i < num_lines; i++) {
        free(split_batch[i]);
    }
    free(split_batch);
}

static void print_samples(array_list_t* sample_list) {
    vcf_annot_sample_t *annot_sample;
    vcf_annot_chr_t *annot_chr;
    vcf_annot_pos_t *annot_pos;
    
    for(int i = 0; i < array_list_size(sample_list); i++) {
        annot_sample = (vcf_annot_sample_t*) array_list_get(i, sample_list);
        printf ( "Sample: %s\n", annot_sample->name );
        for(int j = 0; j < array_list_size(annot_sample->chromosomes); j++) {
            annot_chr = (vcf_annot_chr_t*) array_list_get(j, annot_sample->chromosomes);
            printf ( "Chr: %s(%d)\n", annot_chr->name , array_list_size(annot_chr->positions));
        }
    }
}

int invoke_snp_ws(const char *url, vcf_record_t **records, int num_records) {
    CURLcode ret_code = CURLE_OK;

    const char *output_format = "txt";
    int variants_len = 512, current_index = 0;
    char *variants = (char*) calloc (variants_len, sizeof(char));

    char URL_AUX_1[4*1028] = "localhost:8080/cellbase/rest/latest/hsa/genomic/position/";
    char URL_AUX_2[]="/snp";
    char *URL;
    
    int chr_len, new_len_range;

    for (int i = 0; i < num_records; i++) {
        vcf_record_t *record = records[i];
        if (!strcmp(".", record->id)) {
            continue;
        }
        
        chr_len = record->chromosome_len;
        new_len_range = current_index + chr_len + 32;
        
        // Reallocate memory if next record won't fit
        if (variants_len < (current_index + new_len_range + 1)) {
            char *aux = (char*) realloc(variants, (variants_len + new_len_range + 1) * sizeof(char));
            if (aux) { 
                variants = aux; 
                variants_len += new_len_range;
            }
        }
        
        // Append region info to buffer
        strncat(variants, record->chromosome, chr_len);
        strncat(variants, ":", 1);
        current_index += chr_len + 1;
        sprintf(variants + current_index, "%lu", record->position);
        strncat(variants, ",", 1);
        current_index = strlen(variants); // TODO cambiar nombre
    }
    // PROVISIONAL
    URL = (char*) calloc(strlen(URL_AUX_1) + strlen(variants) + strlen(URL_AUX_2) +1, sizeof(char));
    strcpy(URL, URL_AUX_1);
    strcat(URL, variants);
    strcat(URL, URL_AUX_2);

    if (current_index > 0) {
        char *params[2] = { "of", "geneId" };
        char *params_values[2] = { output_format, variants };
//        ret_code = http_post(url, params, params_values, 2, save_snp_response, NULL);
        ret_code = http_get(URL, NULL, NULL, 0, save_snp_response, NULL);
    }
    
    free(variants);
    
    return ret_code;
}

static size_t save_snp_response(char *contents, size_t size, size_t nmemb, void *userdata) {
    int tid = omp_get_thread_num();
    
    strncat(snp_line[tid], contents, size * nmemb);
    
    char *buffer = realloc (snp_line[tid], snp_max_line_size[tid] + size * nmemb);
    if (buffer) {
        snp_line[tid] = buffer;
        max_line_size[tid] += size * nmemb;
    } else {
        LOG_FATAL("Error while allocating memory for SNP phenotype web service response");
    }

    return size * nmemb;
}

static void vcf_annot_parse_snp_response(int tid, vcf_record_t ** variants, int num_variants) { 
    char *copy_buf;
    int num_lines;
    vcf_record_t * record;
    char **split_batch = split(snp_line[tid], "\n", &num_lines);
    
    for (int i = 0, j = 0; i < num_lines; i++) {
        int num_columns;
        char *copy_buf = strdup(split_batch[i]);
        char **split_result = split(copy_buf, "\t", &num_columns);
        free(copy_buf);
        
        // Find consequence type name (always after SO field)
        if (num_columns == 6) {
            
            for(; j < num_variants; j++){
                record = variants[j];

                if( strncmp(split_result[1], record->chromosome, record->chromosome_len) == 0 &&  atoi(split_result[2]) == record->position){
                    set_vcf_record_id(strdup(split_result[0]), strlen(split_result[0]), record);
                    break;
                }
            }
        } else {
            if (strlen(split_batch[i]) == 0) { // Last line in batch could be only a newline
                continue;
            }
            LOG_INFO_F("[%d] Non-valid line found (%d fields): '%s'\n", tid, num_columns, split_batch[i]);
        }
        
        for (int s = 0; s < num_columns; s++) {
            free(split_result[s]);
        }
        free(split_result);
    }
    
    for (int i = 0; i < num_lines; i++) {
        free(split_batch[i]);
    }
    free(split_batch);
}

int set_field_value_in_info(char *key, char *value, bool append, vcf_record_t *record){
   
    char *info = strndup(record->info, record->info_len);
    char **splits_info;
    char **splits_key;
    char *aux_string;
    char *key_string;
    char *val_string;
    int num_splits, num_key_val;
    char *copy_buf;
    int find = 0;

    if(strcmp(info, ".") == 0){
        copy_buf = (char*) calloc(strlen(key) + strlen(value) + 1 + 1, sizeof(char));
        strcpy(copy_buf, key);
        strcat(copy_buf, "=");
        strcat(copy_buf, value);
        record->info = copy_buf;
        record->info_len = strlen(copy_buf);
        free(info);
        return 1;
    }else{
        splits_info = split(info, ";", &num_splits);
        copy_buf = (char*) calloc(record->info_len + strlen(key) + strlen(value) + 1 + 1 + 1, sizeof(char));
        for(int i = 0; i < num_splits; i++){
            aux_string = strdup(splits_info[i]);
            splits_key = split(aux_string, "=", &num_key_val);
            key_string = strdup(splits_key[0]);
            val_string = strdup(splits_key[1]);
            if(strcmp(key, key_string) == 0){
                strcat(copy_buf, key_string);
                strcat(copy_buf, "=");
                if(append){
                    strcat(copy_buf, val_string);
                }
                if(!strstr(val_string, value)){
                    if(append)
                        strcat(copy_buf, ",");
                    strcat(copy_buf, value);    
                }
                find = 1;
            }else{
                strcat(copy_buf, key_string);
                strcat(copy_buf, "=");
                strcat(copy_buf, val_string);
            }
            if(i < (num_splits - 1)){
                strcat(copy_buf, ";");
            }

            free(splits_key[0]);
            free(splits_key[1]);
            free(key_string);
            free(val_string);
            free(aux_string);
        }
        if(find == 0){
            strcat(copy_buf, ";");
            strcat(copy_buf, key);
            strcat(copy_buf, "=");
            strcat(copy_buf, value);
        }
        record->info = copy_buf;
        record->info_len = strlen(copy_buf);
        free(info);
        return 1;
    }
        free(info);
    return 0;
}
