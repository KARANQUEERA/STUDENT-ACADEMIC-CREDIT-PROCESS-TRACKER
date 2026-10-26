#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
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

    courses = load_courses_from_csv("data/course_catalog_final_final_final.csv");
    if (courses == NULL) {
        printf("\nNo courses loaded. Check that this file exists:\n");
        printf("data/course_catalog_final_final_final.csv\n");
        printf("Run this program from the project root folder.\n");
        return 1;
    }

    show_courses_for_profile(courses, semester, specialization, group);
    track_courses_for_profile(courses, semester, specialization, group,
                              "data/course_completion.csv");
    free_courses(courses);
    return 0;
}
