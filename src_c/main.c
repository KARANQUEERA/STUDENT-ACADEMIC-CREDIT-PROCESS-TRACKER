#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include "course_list.h"

static int read_number(const char *prompt, int minimum, int maximum)
{
    char line[100], *end;
    long value;
    for (;;) {
        printf("%s", prompt);
        if (!fgets(line, sizeof(line), stdin)) return minimum;
        value = strtol(line, &end, 10);
        while (isspace((unsigned char)*end)) end++;
        if (end != line && *end == '\0' && value >= minimum && value <= maximum)
            return (int)value;
        printf("Invalid choice. Enter a whole number from %d to %d.\n", minimum, maximum);
    }
}

static int read_text(const char *prompt, char *text, size_t capacity)
{
    size_t length;
    int character;

    for (;;) {
        printf("%s", prompt);
        if (fgets(text, (int)capacity, stdin) == NULL) return 0;
        length = strlen(text);
        if (length > 0 && text[length - 1] == '\n') {
            text[length - 1] = '\0';
            return 1;
        }
        if (feof(stdin)) return 1;
        while ((character = getchar()) != '\n' && character != EOF) { }
        printf("Input is too long. Please try again.\n");
    }
}

static void display_search_results(const Course **matches, int count)
{
    int index;
    for (index = 0; index < count; ++index) {
        const Course *course = matches[index];
        printf("Semester %d | %s | %s | %s | specialization %s | group %s | %d credits | %s\n",
               course->semester, course->code, course->name, course->type,
               course->specialization, course->group, course->credits,
               course->completed ? "COMPLETED" : "NOT COMPLETED");
    }
}

static int run_course_search(const Course *courses)
{
    char query[160];
    char specialization[100];
    const char *type = NULL;
    const char *group = NULL;
    const Course **matches;
    int semester, type_choice, group_choice, status_choice;
    int completion_filter = -1;
    int count;

    if (!read_text("Code/name contains (blank for any): ", query, sizeof(query)))
        return 0;
    semester = read_number("Semester (0 any, 1-8): ", 0, 8);
    if (!read_text("Specialization (blank for any): ", specialization,
                   sizeof(specialization))) return 0;
    type_choice = read_number("Type (0 any, 1 MAIN, 2 ELECTIVE): ", 0, 2);
    group_choice = read_number("Group (0 any, 1 CHEMISTRY, 2 PHYSICS, 3 BOTH): ", 0, 3);
    status_choice = read_number("Status (0 any, 1 completed, 2 not completed): ", 0, 2);

    if (type_choice == 1) type = "MAIN";
    else if (type_choice == 2) type = "ELECTIVE";
    if (group_choice == 1) group = "CHEMISTRY";
    else if (group_choice == 2) group = "PHYSICS";
    else if (group_choice == 3) group = "BOTH";
    if (status_choice == 1) completion_filter = 1;
    else if (status_choice == 2) completion_filter = 0;

    count = search_courses(courses, query, semester, type,
                           specialization[0] ? specialization : NULL,
                           group, completion_filter, NULL, 0);
    if (count < 0) {
        printf("Search filters were invalid. No results were changed.\n");
        return 1;
    }
    if (count == 0) {
        printf("No courses matched those search filters.\n");
        return 1;
    }
    matches = (const Course **)malloc((size_t)count * sizeof(*matches));
    if (matches == NULL) {
        printf("Not enough memory to display the search results.\n");
        return 1;
    }
    search_courses(courses, query, semester, type,
                   specialization[0] ? specialization : NULL,
                   group, completion_filter, matches, count);
    printf("\nSearch results (%d courses):\n", count);
    display_search_results(matches, count);
    free(matches);
    return 1;
}

static void run_course_tools(const Course *courses, const char *specialization)
{
    int choice;
    for (;;) {
        printf("\nAdditional course tools:\n");
        printf("1. Search courses\n");
        printf("2. Completed-credit report\n");
        printf("0. Finish\n");
        choice = read_number("Choose an option: ", 0, 2);
        if (choice == 0) return;
        if (choice == 1) {
            if (!run_course_search(courses)) return;
        } else {
            const char *report_group = "BOTH";
            int group_choice;
            printf("For Semester 1-2 courses, select your known group:\n");
            printf("1. Chemistry  2. Physics  3. Unknown/include both\n");
            group_choice = read_number("Group: ", 1, 3);
            if (group_choice == 1) report_group = "CHEMISTRY";
            else if (group_choice == 2) report_group = "PHYSICS";
            print_completed_credit_report(courses, specialization, report_group);
        }
    }
}

int main(void)
{
    Course *courses;
    int semester;
    int choice;
    const char *specialization;
    const char *group = "BOTH";

    printf("============================================\n");
    printf(" STUDENT ACADEMIC CREDIT PROCESS TRACKER\n");
    printf("============================================\n");
    printf("Choose specialization:\n");
    printf("1. CSE Core\n");
    printf("2. AI/ML\n");
    printf("3. AI and Data Science\n");
    printf("4. Cyber Security\n");
    choice = read_number("Enter choice (1-4): ", 1, 4);

    if (choice == 1) specialization = "CORE";
    else if (choice == 2) specialization = "AIML";
    else if (choice == 3) specialization = "DS";
    else specialization = "CS";

    semester = read_number("Enter your current semester (1-8): ", 1, 8);

    if (semester == 1 || semester == 2) {
        int group_choice;
        printf("\nWhich group are you in for Semester %d?\n", semester);
        printf("1. Chemistry Group\n");
        printf("2. Physics Group\n");
        group_choice = read_number("Enter choice (1 or 2): ", 1, 2);

        group = (group_choice == 1) ? "CHEMISTRY" : "PHYSICS";
    }

    courses = load_courses_from_csv("data/course_catalog_with_difficulty.csv");
    if (courses == NULL) {
        printf("\nNo courses loaded. Check that this file exists:\n");
        printf("data/course_catalog_with_difficulty.csv\n");
        printf("Run this program from the project root folder.\n");
        return 1;
    }

    show_courses_for_profile(courses, semester, specialization, group);
    if (!track_courses_for_profile(courses, semester, specialization, group,
                                   "data/course_completion.csv")) {
        free_courses(courses);
        return 1;
    }
    run_course_tools(courses, specialization);
    free_courses(courses);
    return 0;
}
