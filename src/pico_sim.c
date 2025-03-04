/* Simple simulation program, using the picoquic simulation library.
* Takes as input a simulation specification as a text file.
* Runs the test, produces outputs as specified.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include "picoquic.h"
#include "picoquic_ns.h"
#include "picoquic_utils.h"

int parse_spec_file(picoquic_ns_spec_t* spec, FILE* F);
void release_spec_data(picoquic_ns_spec_t* spec);

#ifdef _WINDOWS
#include "../pico_sim_vs/pico_sim_vs/getopt.h"
#ifdef _WINDOWS64
#define PICOQUIC_DIR "../../../../picoquic"
#else
#define PICOQUIC_DIR "../../../picoquic"
#endif
#else
#define PICOQUIC_DIR "../picoquic"
#endif

void usage()
{
    fprintf(stderr, "Pico_sim, picoquic network simularor\n\n");
    fprintf(stderr, "Usage: pico_sim [options] simulation_specification\n\n");
    fprintf(stderr, "Examples of simulation specifications are found in the\n");
    fprintf(stderr, "folder \"sim_specs\"\n");
    fprintf(stderr, "Pico_sim options:\n");
    fprintf(stderr, "  -S path  Path to the picoquic source directory, where the\n");
    fprintf(stderr, "           code will find the key and certificates used for\n");
    fprintf(stderr, "           setting test connections.\n");
    fprintf(stderr, "  -h       Print this message.\n");
}

int main(int argc, char** argv)
{
    int ret = 0;
    picoquic_ns_spec_t spec = { 0 };
    FILE* F = NULL;
    char const * spec_file_name = NULL;
    char const* source_dir = PICOQUIC_DIR;
    char const* option_string = "S:h";
    int opt;

    /* Get the parameters */
    while ((opt = getopt(argc, argv, option_string)) != -1) {
        switch (opt) {
        case 'S':
            source_dir = optarg;
            break;
        case 'h':
            usage();
            exit(0);
        default:
            usage();
            exit(-1);
        }
    }
    picoquic_set_solution_dir(source_dir);

    if (optind >= argc || optind + 1 < argc) {
        fprintf(stderr, "Unexpected arguments.\n");
        usage();
        ret = -1;
    }
    else if ((F = picoquic_file_open((spec_file_name = argv[optind]), "r")) == NULL) {
        fprintf(stderr, "Cannot open file <%s>\n", spec_file_name);
        ret = -1;
    }
    else
    {
        if (parse_spec_file(&spec, F) != 0) {
            fprintf(stderr, "Error when processing file <%s>\n", spec_file_name);
        }
        else {
            ret = picoquic_ns(&spec);
            printf("picoquic_ns (%s) returns %d\n", spec_file_name, ret);
        }
        F = picoquic_file_close(F);
        release_spec_data(&spec);
    }
    return ret;
}

typedef enum {
    e_main_start_time = 0,
    e_background_start_time,
    e_main_scenario_text,
    e_background_scenario_text,
    e_main_cc_algo,
    e_background_cc_algo,
    e_nb_connections,
    e_main_target_time,
    e_data_rate_in_gbps,
    e_latency,
    e_jitter,
    e_queue_delay_max,
    e_l4s_max,
    e_icid,
    e_qlog_dir,
    e_error
} spec_param_enum;

typedef struct st_spec_param_t {
    spec_param_enum p_e;
    char const* p_name;
    size_t p_len;
} spec_param_t;

spec_param_t params[] = {
    { e_main_start_time, "main_start_time", 15 },
    { e_main_target_time, "main_target_time", 16 },
    { e_background_start_time, "background_start_time", 21 },
    { e_main_scenario_text, "main_scenario_text", 18  },
    { e_background_scenario_text, "background_scenario_text", 24 },
    { e_main_cc_algo, "main_cc_algo", 12 },
    { e_background_cc_algo, "background_cc_algo", 18 },
    { e_nb_connections, "nb_connections", 14 },
    { e_data_rate_in_gbps, "data_rate_in_gbps", 17 },
    { e_latency, "latency" , 7},
    { e_jitter, "jitter", 6 },
    { e_queue_delay_max, "queue_delay_max", 15 },
    { e_l4s_max, "l4s_max", 7 },
    { e_icid, "icid", 4 },
    { e_qlog_dir, "qlog_dir", 8 }
};

const size_t nb_params = sizeof(params) / sizeof(spec_param_t);

int parse_param(picoquic_ns_spec_t* spec, spec_param_enum p_e, char const * text);

int parse_spec_file(picoquic_ns_spec_t * spec, FILE* F)
{
    int ret = 0;
    char line[1024];

    memset(spec, 0, sizeof(picoquic_ns_spec_t));

    while (ret == 0 && fgets(line, sizeof(line), F) != NULL) {
        spec_param_enum p_e = e_error;
        size_t p_len = 0;
        size_t len = strlen(line);

        while (len > 0 && isspace(line[len - 1]))
        {
            len--;
            line[len] = 0;
        }

        if (len > 0) {
            for (int i = 0; i < nb_params; i++) {
                if (len > params[i].p_len &&
                    strncmp(line, params[i].p_name, params[i].p_len) == 0)
                {
                    p_e = params[i].p_e;
                    p_len = params[i].p_len;
                    break;
                }
            }

            if (p_e == e_error) {
                fprintf(stderr, "Incorrect specification line: %s\n", line);
                ret = -1;
                break;
            }
            else {
                ret = parse_param(spec, p_e, line + p_len);
            }
        }
    }
    return ret;
}

int parse_u64(uint64_t* x, char const* val);
int parse_int(int* x, char const* val);
int parse_double(double* x, char const* val);
int parse_cc_algo(picoquic_congestion_algorithm_t** x, char const* val);
int parse_cid(picoquic_connection_id_t* x, char const* val);
int parse_text(char const** x, char const* val);
int parse_file_name(char const** x, char const* val);
void release_text(char const** text);

int parse_param(picoquic_ns_spec_t* spec, spec_param_enum p_e, char const* line)
{
    int ret = 0;
    /* Skip the colon and spaces */

    while (isspace(line[0])) {
        line++;
    }
    if (line[0] != ':') {
        ret = -1;
    }
    else {
        line++;
        while (isspace(line[0])) {
            line++;
        }
        switch (p_e) {
        case e_main_start_time:
            ret = parse_u64(&spec->main_start_time, line);
            break;
        case e_main_target_time:
            ret = parse_u64(&spec->main_target_time, line);
            break;
        case e_background_start_time:
            ret = parse_u64(&spec->background_start_time, line);
            break;
        case e_main_scenario_text:
            ret = parse_text(&spec->main_scenario_text, line);
            break;
        case e_background_scenario_text:
            ret = parse_text(&spec->background_scenario_text, line);
            break;
        case e_main_cc_algo:
            ret = parse_cc_algo(&spec->main_cc_algo, line);
            break;
        case e_background_cc_algo:
            ret = parse_cc_algo(&spec->background_cc_algo, line);
            break;
        case e_nb_connections:
            ret = parse_int(&spec->nb_connections, line);
            break;
        case e_data_rate_in_gbps:
            ret = parse_double(&spec->data_rate_in_gbps, line);
            break;
        case e_latency:
            ret = parse_u64(&spec->latency, line);
            break;
        case e_jitter:
            ret = parse_u64(&spec->jitter, line);
            break;
        case e_queue_delay_max:
            ret = parse_u64(&spec->queue_delay_max, line);
            break;
        case e_l4s_max:
            ret = parse_u64(&spec->l4s_max, line);
            break;
        case e_icid:
            ret = parse_cid(&spec->icid, line);
            break;
        case e_qlog_dir:
            ret = parse_file_name(&spec->qlog_dir, line);
            break;
        default:
            fprintf(stderr, "Incorrect specification line: %s\n", line);
            break;
        }
    }

    return ret;
}

void release_spec_data(picoquic_ns_spec_t* spec)
{
    release_text(&spec->main_scenario_text);
    release_text(&spec->background_scenario_text);
    release_text(&spec->qlog_dir);
}

int parse_u64(uint64_t* x, char const* val)
{
    int ret = 0;
    uint64_t v = 0;
    int i = 0;

    while (isdigit(val[i])) {
        v *= 10;
        v += val[i] - '0';
        i++;
    }
    if (val[i] != 0) {
        ret = -1;
    }
    else {
        *x = v;
    }
    return ret;
}

int parse_int(int* x, char const* val)
{
    uint64_t v = 0;
    int ret = parse_u64(&v, val);

    if (ret == 0) {
        if (v > 0x7ffffff) {
            ret = -1;
        }
        else {
            *x = (int)v;
        }
    }
    return ret;
}

int parse_double(double* x, char const* val)
{
    int ret = 0;
    double v = 0;
    int i = 0;

    while (isdigit(val[i])) {
        v *= 10;
        v += val[i] - '0';
        i++;
    }
    if (val[i] == '.') {
        double decimal = 1;
        i++; 
        while (isdigit(val[i])) {
            decimal /= 10;
            v += (val[i] - '0') * decimal;
            i++;
        }
    }
    if (val[i] != 0) {
        ret = -1;
    }
    else {
        *x = v;
    }
    return ret;
}

int parse_cc_algo(picoquic_congestion_algorithm_t** x, char const* val)
{
    int ret = 0;
    if (strcmp(val, picoquic_newreno_algorithm->congestion_algorithm_id) == 0) {
        *x = picoquic_newreno_algorithm;
    }
    else if (strcmp(val, picoquic_cubic_algorithm->congestion_algorithm_id) == 0) {
        *x = picoquic_cubic_algorithm;
    }
    else if (strcmp(val, picoquic_dcubic_algorithm->congestion_algorithm_id) == 0) {
        *x = picoquic_dcubic_algorithm;
    }
    else if (strcmp(val, picoquic_fastcc_algorithm->congestion_algorithm_id) == 0) {
        *x = picoquic_fastcc_algorithm;
    }
    else if (strcmp(val, picoquic_bbr_algorithm->congestion_algorithm_id) == 0) {
        *x = picoquic_bbr_algorithm;
    }
    else if (strcmp(val, picoquic_prague_algorithm->congestion_algorithm_id) == 0) {
        *x = picoquic_prague_algorithm;
    }
    else if (strcmp(val, picoquic_bbr1_algorithm->congestion_algorithm_id) == 0) {
        *x = picoquic_bbr1_algorithm;
    }
    else {
        ret = -1;
    }
    return ret;
}

static int hexdigit(char v)
{
    int y = -1;

    if (v >= '0' && v <= '9') {
        y = v - '0';
    }
    else if (v >= 'A' && v <= 'Z') {
        y = 10 + v - 'A';
    }
    else if (v >= 'a' && v <= 'z') {
        y = 10 + v - 'a';
    }
    return y;
}

int parse_cid(picoquic_connection_id_t* x, char const* val)
{
    int ret = 0;
    int i = 0;
    int j = 0;
    int k = 0;
    int u8 = 0;


    while (val[i] != 0) {
        int hx = hexdigit(val[i]);
        if (hx < 0) {
            ret = -1;
            break;
        }
        else if (j >= 8) {
            ret = -1;
            break;
        }
        else {
            u8 += (uint8_t)hx;
            k++;
            if (k == 2) {
                x->id[j] = u8;
                j++;
                u8 = 0;
                k = 0;
            }
            else {
                u8 <<= 4;
                x->id[j] = u8;
            }
        }
        i++;
    }
    if (ret == 0) {
        j++;
        while (j < 8) {
            x->id[j] = 0;
            j++;
        }
        x->id_len = 8;
    }

    if (ret == 0 && val[j] != 0) {
        ret = -1;
    }
    return ret;
}

int parse_text(char const** x, char const* val)
{
    int ret = 0;
    size_t l = strlen(val);

    if (val > 0) {
        char* y = malloc(l + 1);
        if (y == NULL) {
            ret = -1;
        }
        else {
            memcpy(y, val, l);
            y[l] = 0;
            *x = y;
        }
    }
    return ret;
}

int parse_file_name(char const** x, char const* val)
{
#ifdef _WINDOWS
    /* For windows, replace slashes by whacks */
    int i = 0;
    char line[1024];

    while (i < 1024 && val[i] != 0) {
        if (val[i] == '/') {
            line[i] = '\\';
        }
        else {
            line[i] = val[i];
        }
        i++;
    }
    line[i] = 0;
    return parse_text(x, line);
#else
    return parse_text(x, val);
#endif
}

void release_text(char const** text)
{
    if (*text != NULL) {
        free((void*)*text);
        *text = NULL;
    }
}
