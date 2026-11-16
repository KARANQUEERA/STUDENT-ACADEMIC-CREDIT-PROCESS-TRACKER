#ifndef COURSE_LIST_H
/*ifndef = if n not defined */
#define COURSE_LIST_H

#define MAX_CODE 24
#define MAX_NAME 160
#define MAX_TYPE 40
#define MAX_SPECIALIZATION 100
#define MAX_GROUP 20

typedef struct Course {
    int semester;
    char code[MAX_CODE];
    char name[MAX_NAME];
    int credits;
    char type[MAX_TYPE];
    char specialization[MAX_SPECIALIZATION];
    char group[MAX_GROUP];
    int completed;
    struct Course *next;
} Course;

Course *load_courses_from_csv(const char *filename);
void show_courses_for_profile(const Course *head, int semester,
                              const char *specialization, const char *group);
int track_courses_for_profile(Course *head, int semester,
                              const char *specialization, const char *group,
                              const char *status_file);
int search_courses(const Course *head, const char *query, int semester,
                   const char *type, const char *specialization,
                   const char *group, int completion_filter,
                   const Course **matches, int capacity);
void print_completed_credit_report(const Course *head,
                                   const char *specialization,
                                   const char *group);
void free_courses(Course *head);

#endif
