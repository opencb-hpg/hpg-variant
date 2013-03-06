/*
 * Copyright (c) 2012 Cristina Yenyxe Gonzalez Garcia (ICM-CIPF)
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

#include "shared_options.h"

static size_t buffer_size;

shared_options_t *new_shared_cli_options(void) {
    shared_options_t *options_data = (shared_options_t*) calloc (1, sizeof(shared_options_t));
    
    options_data->vcf_filename = arg_file1("v", "vcf-file", NULL, "VCF file used as input");
    options_data->ped_filename = arg_file1("p", "ped-file", NULL, "PED file used as input");
    options_data->output_filename = arg_file0(NULL, "out", NULL, "Filename prefix for main output files");
    options_data->output_directory = arg_str0(NULL, "outdir", NULL, "Directory where the output files will be stored");
    
    options_data->host_url = arg_str0(NULL, "url", NULL, "URL of the host where remote web services run");
    options_data->version = arg_str0(NULL, "version", NULL, "Version of the web service to query");
    options_data->species = arg_str0(NULL, "species", NULL, "Species whose genome is taken as reference");
    
    options_data->max_batches = arg_int0(NULL, "num-batches", NULL, "Maximum number of batches stored at the same time");
    options_data->batch_lines = arg_int0(NULL, "batch-lines", NULL, "Maximum number of lines in a batch");
    options_data->batch_bytes = arg_int0(NULL, "batch-bytes", NULL, "Maximum number of bytes in a batch");
    options_data->num_threads = arg_int0(NULL, "num-threads", NULL, "Number of threads when a task runs in parallel");
    options_data->entries_per_thread = arg_int0(NULL, "entries-per-thread", NULL, "Number of entries from a batch processed by a thread");
    
    options_data->num_alleles = arg_int0(NULL, "alleles", NULL, "Filter: by number of alleles");
    options_data->coverage = arg_int0(NULL, "coverage", NULL, "Filter: by minimum coverage");
    options_data->quality = arg_int0(NULL, "quality", NULL, "Filter: by minimum quality");
    options_data->maf = arg_dbl0(NULL, "maf", NULL, "Filter: by maximum MAF (minimum allele frequency, decimal like 0.01)");
    options_data->missing = arg_dbl0(NULL, "missing", NULL, "Filter: by maximum missing values (decimal like 0.1)");
    options_data->gene = arg_str0(NULL, "gene", NULL, "Filter: by a comma-separated list of genes");
    options_data->region = arg_str0(NULL, "region", NULL, "Filter: by a list of regions (chr1:start1-end1,chr2:start2-end2...)");
    options_data->region_file = arg_file0(NULL, "region-file", NULL, "Filter: by a list of regions (read from a GFF file)");
    options_data->snp = arg_str0(NULL, "snp", NULL, "Filter: by being a SNP or not");
    
    options_data->config_file = arg_file0(NULL, "config", NULL, "File that contains the parameters for configuring the application");
    
    options_data->mmap_vcf_files = arg_lit0(NULL, "mmap-vcf", "Whether to map VCF files to virtual memory or use the I/O API");
    
    options_data->num_options = NUM_GLOBAL_OPTIONS;
    
    return options_data;
}

shared_options_data_t* new_shared_options_data(shared_options_t* options) {
    shared_options_data_t *options_data = (shared_options_data_t*) calloc (1, sizeof(shared_options_data_t));
    
    options_data->vcf_filename = strdup(*(options->vcf_filename->filename));
    options_data->ped_filename = strdup(*(options->ped_filename->filename));
    options_data->output_filename = strdup(*(options->output_filename->filename));
    options_data->output_directory = strdup(*(options->output_directory->sval));
    
    options_data->host_url = strdup(*(options->host_url->sval));
    options_data->version = strdup(*(options->version->sval));
    options_data->species = strdup(*(options->species->sval));
    
    options_data->max_batches = *(options->max_batches->ival);
    options_data->batch_lines = *(options->batch_lines->ival);
    options_data->batch_bytes = *(options->batch_bytes->ival);
    options_data->num_threads = *(options->num_threads->ival);
    options_data->entries_per_thread = *(options->entries_per_thread->ival);
    
    filter_t *filter;
    if (options->num_alleles->count > 0) {
        filter = num_alleles_filter_new(*(options->num_alleles->ival));
        options_data->chain = add_to_filter_chain(filter, options_data->chain);
        LOG_DEBUG_F("number of alleles filter = %d\n", *(options->num_alleles->ival));
    }
    if (options->coverage->count > 0) {
        filter = coverage_filter_new(*(options->coverage->ival));
        options_data->chain = add_to_filter_chain(filter, options_data->chain);
        LOG_DEBUG_F("minimum coverage filter = %d\n", *(options->coverage->ival));
    }
    if (options->quality->count > 0) {
        filter = quality_filter_new(*(options->quality->ival));
        options_data->chain = add_to_filter_chain(filter, options_data->chain);
        LOG_DEBUG_F("minimum quality filter = %d\n", *(options->quality->ival));
    }
    if (options->maf->count > 0) {
        filter = maf_filter_new(*(options->maf->dval));
        options_data->chain = add_to_filter_chain(filter, options_data->chain);
        LOG_DEBUG_F("maximum MAF = %.3f\n", ((maf_filter_args*)filter->args)->max_maf);
    }
    if (options->missing->count > 0) {
        filter = missing_values_filter_new(*(options->missing->dval));
        options_data->chain = add_to_filter_chain(filter, options_data->chain);
        LOG_DEBUG_F("maximum missing values = %.3f\n", ((missing_values_filter_args*)filter->args)->max_missing);
    }
    if (options->snp->count > 0) {
        filter = snp_filter_new(strcmp(*(options->snp->sval), "exclude"));
        options_data->chain = add_to_filter_chain(filter, options_data->chain);
        LOG_DEBUG_F("snp filter to %s SNPs\n", *(options->snp->sval));
    }
    
    if (options->gene->count > 0) {
	// url
	char* url = ws_request(*(options->host_url->sval), *(options->species->sval), *(options->version->sval), *(options->gene->sval));

	// http get
	init_http_environment(0);
	
	// Output buffer
	buffer_size = CURL_MAX_WRITE_SIZE;
	char *buffer = NULL;
	http_get(url, NULL, NULL, 0, write_function_print, &buffer);
	
	assert(buffer);
	
	char* values_str = get_fields(buffer);
	printf("values_str = %s \n",values_str); 
        
	
        filter = region_exact_filter_new(strdup(values_str), 0,
                                         *(options->host_url->sval), *(options->species->sval), *(options->version->sval));
        options_data->chain = add_to_filter_chain(filter, options_data->chain);
        LOG_DEBUG_F("gene = %s\n", *(options->gene->sval));
    } 
    
    if (options->region->count > 0) {
        filter = region_exact_filter_new(strdup(*(options->region->sval)), 0,
                                         *(options->host_url->sval), *(options->species->sval), *(options->version->sval));
        options_data->chain = add_to_filter_chain(filter, options_data->chain);
        LOG_DEBUG_F("regions = %s\n", *(options->region->sval));
    } 
    
    if (options->region_file->count > 0) {
        filter = region_exact_filter_new(strdup(*(options->region->sval)), 1, 
                                         *(options->host_url->sval), *(options->species->sval), *(options->version->sval));
        options_data->chain = add_to_filter_chain(filter, options_data->chain);
        LOG_DEBUG_F("regions file = %s\n", *(options->region->sval));
    }
    
    // If not previously defined, set the value present in the command-line
    if (!mmap_vcf) {
        mmap_vcf = options->mmap_vcf_files->count;
    }
    
    return options_data;
}


void free_shared_options_data(shared_options_data_t *options_data) {
    if (options_data->vcf_filename)     { free(options_data->vcf_filename); }
    // TODO ped filename freed in ped_close
//     if (options_data->ped_filename)     { free(options_data->ped_filename); }
    if (options_data->output_directory) { free(options_data->output_directory); }
    if (options_data->output_filename)  { free(options_data->output_filename); }
    if (options_data->host_url)         { free(options_data->host_url); }
    if (options_data->version)          { free(options_data->version); }
    if (options_data->species)          { free(options_data->species); }
    free(options_data);
}

int read_shared_configuration(const char *filename, shared_options_t *options) {
    if (filename == NULL || options == NULL) {
        return -1;
    }
    
    config_t *config = (config_t*) calloc (1, sizeof(config_t));
    int ret_code = config_read_file(config, filename);
    if (ret_code == CONFIG_FALSE) {
        LOG_ERROR_F("Configuration file error: %s\n", config_error_text(config));
        return CANT_READ_CONFIG_FILE;
    }
    
    const char *tmp_string;
    
    // Read output directory
    ret_code = config_lookup_string(config, "global.outdir", &tmp_string);
    if (ret_code == CONFIG_FALSE) {
        LOG_WARN("Output folder not found in configuration file, must be set via command-line");
    } else {
        *(options->output_directory->sval) = strdup(tmp_string);
        LOG_DEBUG_F("Output folder = %s (%zu chars)\n", *(options->output_directory->sval), strlen(*(options->output_directory->sval)));
    }
    
    // Read whether to mmap VCF files
    ret_code = config_lookup_bool(config, "global.mmap-vcf", &mmap_vcf);
    if (ret_code == CONFIG_FALSE) {
        LOG_WARN("I/O strategy for VCF files not found in configuration file, must be set via command-line");
    } else {
        LOG_DEBUG_F("VCF files mapped to virtual memory = %d\n", mmap_vcf);
    }
    
    // Read species
    ret_code = config_lookup_string(config, "global.species", &tmp_string);
    if (ret_code == CONFIG_FALSE) {
        LOG_WARN("Species not found in configuration file, must be set via command-line");
    } else {
        *(options->species->sval) = strdup(tmp_string);
        LOG_DEBUG_F("species = %s (%zu chars)\n",
                   *(options->species->sval), strlen(*(options->species->sval)));
    }
    
    // Read database URL
    ret_code = config_lookup_string(config, "global.db-url", &tmp_string);
    if (ret_code == CONFIG_FALSE) {
        LOG_WARN("Web services URL not found in configuration file, must be set via command-line");
    } else {
        *(options->host_url->sval) = strdup(tmp_string);
        LOG_DEBUG_F("web services host URL = %s (%zu chars)\n",
                   *(options->host_url->sval), strlen(*(options->host_url->sval)));
    }
    
    // Read database version
    ret_code = config_lookup_string(config, "global.db-version", &tmp_string);
    if (ret_code == CONFIG_FALSE) {
        LOG_WARN("Version not found in configuration file, must be set via command-line");
    } else {
        *(options->version->sval) = strdup(tmp_string);
        LOG_DEBUG_F("version = %s (%zu chars)\n",
                   *(options->version->sval), strlen(*(options->version->sval)));
    }

    config_destroy(config);
    free(config);
    
    return ret_code;
}

//1:11869-14412, 2:32231-11234,...
char* get_fields(char *buffer){
  int numSubStrings;
  char *dup = strdup(buffer);
  
  // split by tab (common-libs/commons/string_utils.h)
  char** contents_split = split(dup, "\n\t", &numSubStrings);

  // get fields 5-7
  int n_values = 64;
  int aux = 0;
  
  char* values = (char*) calloc (n_values, sizeof(char));
  char *aux_values;
  
  // chr:start-end, ... 
  for (int i = 5; i < numSubStrings; i=i+11) {
    aux = aux + strlen(contents_split[i]) + strlen(contents_split[i+1]) + strlen(contents_split[i+2]) + 4; // Extra for : - , and blank
   
    if (aux > n_values) {
      aux_values = (char*) realloc (values, aux);
      if (aux_values) {
	values = aux_values;
      } else {
	LOG_FATAL("Error while allocating memory for genes position");
      }
    }
    
    strcat(values,contents_split[i]);
    strcat(values,":");

    strcat(values,contents_split[i+1]);
    strcat(values,"-");
    
    strcat(values,contents_split[i+2]);
    
    if (i+11<numSubStrings) {
	strcat(values,",");
    }
    
  }
  
  return values;
}

//Realloc Buffer
size_t write_function_print (char *contents, size_t size, size_t nmemb, void *userdata) {
  
  char **buffer_ptr = (char**) userdata;
  if (buffer_size == CURL_MAX_WRITE_SIZE) { // First call
    *buffer_ptr = calloc(buffer_size, sizeof(char));
  }
  
  assert(*buffer_ptr);
  
  // Copy content into buffer_ptr (contents -> buffer_ptr)
  strncat(*buffer_ptr, contents, size * nmemb); 
  
  // In each call is necessary to resize the buffer (realloc)
  char *buffer = realloc (*buffer_ptr, buffer_size + size * nmemb);
  if (buffer) {
    *buffer_ptr = buffer;
  } else {
    LOG_FATAL("Error while allocating memory for genes position (web service request)");
  }
  
  assert(*buffer_ptr);
  buffer_size += size * nmemb;
  
  return size * nmemb;
}
/////////////////////////////////////////

//http://ws.bioinfo.cipf.es/cellbase/rest/latest/hsa/feature/gene/DDX11L1/info?header=false
/////////////////////////////////////////
char *ws_request(const char *host_url, const char *species, const char *version, const char* genes) {
  
    if (host_url == NULL || version == NULL || species == NULL) {
        return NULL;
    }
    
    // URL Constants
    const char *ws_root_url = "cellbase/rest/";
    const char *ws_name_url = "feature/gene/";
    const char *ws_info = "info?header=false";
    
    // Length of URL parts
    const int host_url_len = strlen(host_url);
    const int ws_root_len = strlen(ws_root_url);
    const int version_len = strlen(version);
    const int species_len = strlen(species);
    const int ws_name_len = strlen(ws_name_url);
    const int genes_len = strlen(genes);
    const int ws_info_len = strlen(ws_info);
    const int result_len = host_url_len + ws_root_len + version_len + species_len + ws_name_len + genes_len + ws_info_len + 5; // Extra for 4*('/') and blank
    
    char *result_url = (char*) calloc (result_len, sizeof(char));
    
    // Host URL
    strncat(result_url, host_url, host_url_len);
    if (result_url[host_url_len - 1] != '/') {
        strncat(result_url, "/", 1);
    }
    
    // Root of the web service
    strncat(result_url, ws_root_url, ws_root_len);
    
    // Version
    strncat(result_url, version, version_len);
    if (result_url[strlen(result_url) - 1] != '/') {
        strncat(result_url, "/", 1);
    }
    
    // Species
    strncat(result_url, species, species_len);
    if (result_url[strlen(result_url) - 1] != '/') {
        strncat(result_url, "/", 1);
    }
    
    // Name of the web service
    strncat(result_url, ws_name_url, ws_name_len);
    
    // Genes
    strncat(result_url, genes, genes_len);
    if (result_url[strlen(result_url) - 1] != '/') {
        strncat(result_url, "/", 1);
    }
    
    // Name of the web service
    strncat(result_url, ws_info, ws_info_len);
    
    printf("La url completa es %s\n", result_url);
    
    return result_url;
}