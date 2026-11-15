include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "course_list.h"

static int specialization_matches(const char *course_values, const char *wanted)
{
    const char *start;

    if (wanted == NULL || wanted[0] == '\0') return 1;
    if (course_values == NULL) return 0;
    start = course_values;
    while (*start != '\0') {
        const char *end = strchr(start, '|');
        const char *token_start = start;
        const char *token_end = end ? end : start + strlen(start);
        size_t length;
        while (token_start < token_end && isspace((unsigned char)*token_start))
            ++token_start;
        while (token_end > token_start && isspace((unsigned char)token_end[-1]))
            --token_end;
        length = (size_t)(token_end - token_start);
        if ((length == 3 && strncmp(token_start, "ALL", 3) == 0) ||
            (strlen(wanted) == length && strncmp(token_start, wanted, length) == 0))
            return 1;
        if (end == NULL) break;
        start = end + 1;
    }
    return 0;
}

static int matches_profile(const Course *course, const char *specialization,
                           const char *group)
{
    if (!specialization_matches(course->specialization, specialization)) return 0;
    if (course->semester > 2 || group == NULL || group[0] == '\0' ||
        strcmp(group, "BOTH") == 0)
        return 1;
    return strcmp(course->group, "BOTH") == 0 || strcmp(course->group, group) == 0;
}

/* Print a completed-course ledger and separate compulsory/elective credits. */
void print_completed_credit_report(const Course *head,
                                   const char *specialization,
                                   const char *group)
{
    const Course *course;
    int last_semester = 0;
    int total_main = 0;
    int total_elective = 0;
    int any_completed = 0;

    for (course = head; course != NULL; course = course->next)
        if (course->semester > last_semester) last_semester = course->semester;

    printf("\nCOMPLETED COURSE CREDIT REPORT\n");
    printf("Credits are grouped by course type; elective options are not compulsory.\n");
    for (int semester = 1; semester <= last_semester; ++semester) {
        int semester_main = 0;
        int semester_elective = 0;

        for (course = head; course != NULL; course = course->next) {
            if (course->semester != semester || !course->completed ||
                !matches_profile(course, specialization, group)) continue;

            any_completed = 1;
            printf("Semester %d | %s | %s | %s | specialization %s | group %s | %d credits\n",
                   semester, course->code, course->name, course->type,
                   course->specialization, course->group, course->credits);
            if (strcmp(course->type, "MAIN") == 0) {
                semester_main += course->credits;
                total_main += course->credits;
            } else if (strcmp(course->type, "ELECTIVE") == 0) {
                semester_elective += course->credits;
                total_elective += course->credits;
            }
        }
        printf("Semester %d totals: %d compulsory credits, %d elective-option credits.\n",
               semester, semester_main, semester_elective);
    }

    if (!any_completed) printf("No completed courses match this profile.\n");
    printf("Completed compulsory credits: %d\n", total_main);
    printf("Completed elective-option credits: %d\n", total_elective);
}
