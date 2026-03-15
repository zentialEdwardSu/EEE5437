#include <stdio.h>

#include "hw1/hw1_text_prob_analyzer.h"

int main(int argc, char* argv[]) {
    dic_hw1_text_analysis analysis;
    dic_hw1_status status;
    char* report;

    if (argc != 2) {
        fprintf(stderr, "usage: hw1 <text-file>\n");
        return 1;
    }

    status = dic_hw1_analyze_file(argv[1], &analysis);
    if (status != DIC_HW1_OK) {
        fprintf(stderr, "error: %s\n", dic_hw1_status_message(status));
        return 1;
    }

    report = dic_hw1_build_report(&analysis);
    if (report == NULL) {
        fprintf(stderr, "error: %s\n", dic_hw1_status_message(DIC_HW1_MEMORY_ERROR));
        return 1;
    }

    printf("%s\n", report);
    dic_hw1_free_report(report);
    return 0;
}
