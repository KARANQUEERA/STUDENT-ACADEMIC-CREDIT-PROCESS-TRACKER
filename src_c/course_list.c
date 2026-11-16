#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <unistd.h>
#endif
#include "course_list.h"

#define CSV_COLUMN_COUNT 18
#define CSV_RECORD_SIZE 8192

static const char *CSV_HEADERS[CSV_COLUMN_COUNT] = {
    "semester", "code", "name", "credits", "type", "specialization", "group",
    "pre_requisite", "offered_by", "base_difficulty", "difficulty_level",
    "difficulty_reason", "key_background_areas", "syllabus_topics", "syllabus_source",
    "research_status", "difficulty_confidence", "elective_category"
};

static void trim(char *text)
{
    size_t len;
    while (*text && isspace((unsigned char)*text)) {
        memmove(text, text + 1, strlen(text));
    }
    len = strlen(text);
    while (len > 0 && isspace((unsigned char)text[len - 1])) {
        text[--len] = '\0';
    }
}

static void copy_field(char *destination, size_t size, const char *source)
{
    if (size == 0) return;
    strncpy(destination, source ? source : "", size - 1);
    destination[size - 1] = '\0';
    trim(destination);
}

/* Read one logical CSV record, allowing quoted fields to span physical lines. */
static int read_csv_record(FILE *file, char *record, size_t capacity)
{
    size_t used = 0;
    int in_quotes = 0;
    int too_long = 0;
    int character;

    while ((character = fgetc(file)) != EOF) {
        if (character == '"') {
            if (in_quotes) {
                int following = fgetc(file);
                if (following == '"') {
                    if (used + 2 < capacity) {
                        record[used++] = '"';
                        record[used++] = '"';
                    } else {
                        too_long = 1;
                    }
                    continue;
                }
                in_quotes = 0;
                if (following != EOF) ungetc(following, file);
            } else {
                in_quotes = 1;
            }
        } else if (character == '\n' && !in_quotes) {
            if (used > 0 && record[used - 1] == '\r') --used;
            if (too_long) return -1;
            record[used] = '\0';
            return 1;
        }

        if (used + 1 < capacity) record[used++] = (char)character;
        else too_long = 1;
    }

    if (used == 0 && !too_long) return 0;
    if (too_long || in_quotes) return -1;
    record[used] = '\0';
    return 1;
}

/* Decode one CSV record in place. Negative means malformed or too many fields. */
static int parse_csv_record(char *record, char *fields[], int max_fields)
{
    char *source = record;
    char *destination = record;
    int count = 0;

    for (;;) {
        int has_comma;
        if (count >= max_fields) return -1;
        fields[count++] = destination;

        if (*source == '"') {
            int closed = 0;
            ++source;
            while (*source) {
                if (*source == '"' && source[1] == '"') {
                    *destination++ = '"';
                    source += 2;
                } else if (*source == '"') {
                    ++source;
                    closed = 1;
                    break;
                } else {
                    *destination++ = *source++;
                }
            }
            if (!closed) return -1;
            if (*source != '\0' && *source != ',') return -1;
        } else {
            while (*source && *source != ',') {
                if (*source == '"') return -1;
                *destination++ = *source++;
            }
        }

        has_comma = (*source == ',');
        if (has_comma) ++source;
        *destination++ = '\0';
        if (!has_comma) break;
    }
    return count;
}

static int valid_csv_header(char *fields[], int count)
{
    int column;
    if (count != CSV_COLUMN_COUNT) return 0;
    if (strlen(fields[0]) >= 3 && (unsigned char)fields[0][0] == 0xef &&
        (unsigned char)fields[0][1] == 0xbb &&
        (unsigned char)fields[0][2] == 0xbf)
        memmove(fields[0], fields[0] + 3, strlen(fields[0] + 3) + 1);
    for (column = 0; column < CSV_COLUMN_COUNT; ++column) {
        if (strcmp(fields[column], CSV_HEADERS[column]) != 0) return 0;
    }
    return 1;
}

static int parse_integer(const char *text, long minimum, long maximum, int *value)
{
    char *end;
    long parsed;
    errno = 0;
    parsed = strtol(text, &end, 10);
    if (text[0] == '\0' || *end != '\0' || errno == ERANGE ||
        parsed < minimum || parsed > maximum) return 0;
    *value = (int)parsed;
    return 1;
}


static int specialization_matches(const char *catalog_value, const char *wanted)
{
    char buffer[MAX_SPECIALIZATION];
    char *token;

    copy_field(buffer, sizeof(buffer), catalog_value);

    token = strtok(buffer, "|");
    while (token != NULL) {
        trim(token);
        if (strcmp(token, "ALL") == 0 || strcmp(token, wanted) == 0) {
            return 1;
        }
        token = strtok(NULL, "|");
    }
    return 0;
}

static int append_course(Course **head, const Course *record)
{
    Course *node = (Course *)malloc(sizeof(Course));
    Course *tail;

    if (node == NULL) {
        fprintf(stderr, "Error: not enough memory to load courses.\n");
        return 0;
    }

    *node = *record;
    node->next = NULL;

    if (*head == NULL) {
        *head = node;
        return 1;
    }

    tail = *head;
    while (tail->next != NULL) tail = tail->next;
    tail->next = node;
    return 1;
}

Course *load_courses_from_csv(const char *filename)
{
    FILE *file = fopen(filename, "r");
    Course *head = NULL;
    char csv_record[CSV_RECORD_SIZE];
    char *fields[CSV_COLUMN_COUNT];
    int record_status;

    if (file == NULL) {
        printf("Error: Could not open '%s'.\n", filename);
        printf("Run the program from the project root folder.\n");
        return NULL;
    }

    record_status = read_csv_record(file, csv_record, sizeof(csv_record));
    if (record_status != 1 ||
        !valid_csv_header(fields, parse_csv_record(csv_record, fields, CSV_COLUMN_COUNT))) {
        fclose(file);
        return NULL;
    }

    while ((record_status = read_csv_record(file, csv_record, sizeof(csv_record))) == 1) {
        Course course_record;
        int count;
        int semester;
        int credits;

        if (csv_record[0] == '\0') continue;
        count = parse_csv_record(csv_record, fields, CSV_COLUMN_COUNT);
        if (count != CSV_COLUMN_COUNT ||
            !parse_integer(fields[0], 1, 8, &semester) ||
            !parse_integer(fields[3], 0, 1000, &credits) ||
            fields[1][0] == '\0' || fields[2][0] == '\0' ||
            fields[4][0] == '\0' || fields[5][0] == '\0' ||
            strlen(fields[1]) >= sizeof(course_record.code) ||
            strlen(fields[2]) >= sizeof(course_record.name) ||
            strlen(fields[4]) >= sizeof(course_record.type) ||
            strlen(fields[5]) >= sizeof(course_record.specialization) ||
            strlen(fields[6]) >= sizeof(course_record.group)) {
            fprintf(stderr, "Error: malformed or incomplete catalog record.\n");
            free_courses(head);
            fclose(file);
            return NULL;
        }

        memset(&course_record, 0, sizeof(course_record));
        course_record.semester = semester;
        course_record.credits = credits;
        copy_field(course_record.code, sizeof(course_record.code), fields[1]);
        copy_field(course_record.name, sizeof(course_record.name), fields[2]);
        copy_field(course_record.type, sizeof(course_record.type), fields[4]);
        copy_field(course_record.specialization, sizeof(course_record.specialization), fields[5]);
        copy_field(course_record.group, sizeof(course_record.group), fields[6]);
        if (course_record.group[0] == '\0') strcpy(course_record.group, "BOTH");
        course_record.completed = 0;

        if (!append_course(&head, &course_record)) {
            free_courses(head);
            fclose(file);
            return NULL;
        }
    }

    fclose(file);
    if (record_status < 0) {
        fprintf(stderr, "Error: malformed or overlong catalog record.\n");
        free_courses(head);
        return NULL;
    }
    return head;
}

static int profile_match(const Course *course, int semester,
                         const char *specialization, const char *group)
{
    int group_match = semester > 2 || strcmp(course->group, "BOTH") == 0 ||
                      strcmp(course->group, group) == 0;
    return course->semester == semester &&
           specialization_matches(course->specialization, specialization) &&
           group_match;
}

typedef struct CompletionRecord {
    int semester;
    char code[MAX_CODE];
    char type[MAX_TYPE];
    char specialization[MAX_SPECIALIZATION];
    char group[MAX_GROUP];
    int completed;
    Course *matched_course;
    struct CompletionRecord *next;
} CompletionRecord;

static int split_completion_line(char *line, char *fields[], int capacity)
{
    int count = 1;
    char *cursor;
    fields[0] = line;
    for (cursor = line; *cursor; ++cursor) {
        if (*cursor == '|') {
            *cursor = '\0';
            if (count >= capacity) return capacity + 1;
            fields[count++] = cursor + 1;
        } else if (*cursor == '\r' || *cursor == '\n') {
            *cursor = '\0';
            break;
        }
    }
    return count;
}

static int parse_completion_integer(const char *text, long minimum, long maximum,
                                    int *value)
{
    char *end;
    long parsed;
    errno = 0;
    parsed = strtol(text, &end, 10);
    if (text[0] == '\0' || end == text || *end != '\0' || errno == ERANGE ||
        parsed < minimum || parsed > maximum) return 0;
    *value = (int)parsed;
    return 1;
}

static Course *find_completion_course(Course *head, const CompletionRecord *record,
                                      int *match_count)
{
    Course *course;
    Course *match = NULL;
    *match_count = 0;
    for (course = head; course; course = course->next) {
        if (course->semester == record->semester &&
            strcmp(course->code, record->code) == 0 &&
            strcmp(course->type, record->type) == 0 &&
            strcmp(course->specialization, record->specialization) == 0 &&
            strcmp(course->group, record->group) == 0) {
            match = course;
            ++*match_count;
        }
    }
    return match;
}

static int same_completion_identity(const CompletionRecord *left,
                                    const CompletionRecord *right)
{
    return left->semester == right->semester &&
           strcmp(left->code, right->code) == 0 &&
           strcmp(left->type, right->type) == 0 &&
           strcmp(left->specialization, right->specialization) == 0 &&
           strcmp(left->group, right->group) == 0;
}

static void free_completion_records(CompletionRecord *records)
{
    while (records) {
        CompletionRecord *next = records->next;
        free(records);
        records = next;
    }
}

/*
 * Read and validate every record before changing any Course status.
 * Extra valid identities from an older catalog are retained for the next save.
 */
static int load_completion_status(Course *head, const char *filename,
                                  CompletionRecord **records_out)
{
    FILE *file;
    char line[512];
    int line_number = 0;
    int record_count = 0;
    CompletionRecord *records = NULL;
    CompletionRecord *tail = NULL;

    *records_out = NULL;
    errno = 0;
    file = fopen(filename, "r");
    if (!file) {
        int open_error = errno;
        if (open_error == ENOENT) {
            printf("No existing completion file; starting with all courses incomplete.\n");
            return 1;
        }
        fprintf(stderr, "Could not open completion file '%s': %s. No changes were saved.\n",
                filename, strerror(open_error));
        return 0;
    }

    while (fgets(line, sizeof(line), file)) {
        CompletionRecord parsed;
        CompletionRecord *previous;
        char *fields[32];
        int field_count;
        int part;
        int match_count;
        size_t used = 0;
        size_t line_length;

        ++line_number;
        line_length = strlen(line);
        if (!strchr(line, '\n') && line_length == sizeof(line) - 1 && !feof(file)) {
            fprintf(stderr, "Completion record on line %d is too long. File was not changed.\n",
                    line_number);
            break;
        }

        field_count = split_completion_line(line, fields, 32);
        if (field_count < 6 || field_count > 32) {
            fprintf(stderr, "Malformed completion record on line %d. File was not changed.\n",
                    line_number);
            break;
        }
        for (part = 0; part < field_count; ++part) trim(fields[part]);

        memset(&parsed, 0, sizeof(parsed));
        if (!parse_completion_integer(fields[0], 1, 8, &parsed.semester) ||
            !parse_completion_integer(fields[field_count - 1], 0, 1,
                                      &parsed.completed) ||
            fields[1][0] == '\0' || fields[2][0] == '\0' ||
            fields[field_count - 2][0] == '\0' ||
            strlen(fields[1]) >= sizeof(parsed.code) ||
            strlen(fields[2]) >= sizeof(parsed.type) ||
            strlen(fields[field_count - 2]) >= sizeof(parsed.group)) {
            fprintf(stderr, "Invalid identity, number, or status on completion line %d. "
                            "File was not changed.\n", line_number);
            break;
        }
        if (strcmp(fields[2], "MAIN") != 0 && strcmp(fields[2], "ELECTIVE") != 0) {
            fprintf(stderr, "Invalid course type on completion line %d. File was not changed.\n",
                    line_number);
            break;
        }

        copy_field(parsed.code, sizeof(parsed.code), fields[1]);
        copy_field(parsed.type, sizeof(parsed.type), fields[2]);
        copy_field(parsed.group, sizeof(parsed.group), fields[field_count - 2]);
        for (part = 3; part < field_count - 2; ++part) {
            size_t token_length = strlen(fields[part]);
            size_t separator = part > 3 ? 1U : 0U;
            if (token_length == 0 || used + separator + token_length >=
                                     sizeof(parsed.specialization)) break;
            if (separator) parsed.specialization[used++] = '|';
            memcpy(parsed.specialization + used, fields[part], token_length);
            used += token_length;
            parsed.specialization[used] = '\0';
        }
        if (part != field_count - 2 || parsed.specialization[0] == '\0') {
            fprintf(stderr, "Invalid specialization on completion line %d. File was not changed.\n",
                    line_number);
            break;
        }

        for (previous = records; previous; previous = previous->next) {
            if (same_completion_identity(previous, &parsed)) break;
        }
        if (previous) {
            fprintf(stderr, "Duplicate course identity on completion line %d. "
                            "File was not changed.\n", line_number);
            break;
        }

        parsed.matched_course = find_completion_course(head, &parsed, &match_count);
        if (match_count > 1) {
            fprintf(stderr, "Completion identity on line %d matches multiple catalog rows. "
                            "File was not changed.\n", line_number);
            break;
        }

        previous = (CompletionRecord *)malloc(sizeof(*previous));
        if (!previous) {
            fprintf(stderr, "Not enough memory to read completion data. File was not changed.\n");
            break;
        }
        *previous = parsed;
        previous->next = NULL;
        if (!records) records = previous;
        else tail->next = previous;
        tail = previous;
        ++record_count;
    }

    if (ferror(file)) {
        fprintf(stderr, "Error reading completion file '%s'. File was not changed.\n", filename);
        line_number = -1;
    }
    if (fclose(file) != 0) {
        fprintf(stderr, "Error closing completion file '%s'. File was not changed.\n", filename);
        line_number = -1;
    }
    if (line_number < 0 || (line_number > 0 && record_count != line_number) ||
        record_count == 0) {
        if (record_count == 0 && line_number >= 0)
            fprintf(stderr, "Completion file '%s' is empty. File was not changed.\n", filename);
        free_completion_records(records);
        return 0;
    }

    {
        CompletionRecord *record;
        for (record = records; record; record = record->next) {
            if (record->matched_course)
                record->matched_course->completed = record->completed;
        }
    }
    *records_out = records;
    return 1;
}

static int write_completion_record(FILE *file, int semester, const char *code,
                                   const char *type, const char *specialization,
                                   const char *group, int completed)
{
    return fprintf(file, "%d|%s|%s|%s|%s|%d\n", semester, code, type,
                   specialization, group, completed) >= 0;
}

static FILE *open_exclusive_temporary_file(const char *filename)
{
    int descriptor;
    FILE *file;
#ifdef _WIN32
    descriptor = _open(filename, _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY,
                       _S_IREAD | _S_IWRITE);
    if (descriptor < 0) return NULL;
    file = _fdopen(descriptor, "wb");
    if (!file) _close(descriptor);
#else
    descriptor = open(filename, O_CREAT | O_EXCL | O_WRONLY, 0600);
    if (descriptor < 0) return NULL;
    file = fdopen(descriptor, "wb");
    if (!file) close(descriptor);
#endif
    return file;
}

static int save_completion_status(const Course *head, const char *filename,
                                  const CompletionRecord *records)
{
    size_t filename_length = strlen(filename);
    char *temporary_name;
    FILE *file;
    const CompletionRecord *record;
    const Course *course;
    int write_failed = 0;

    if (filename_length > (size_t)-1 - 5) {
        fprintf(stderr, "Completion file path is too long; data was not saved.\n");
        return 0;
    }
    temporary_name = (char *)malloc(filename_length + 5);
    if (!temporary_name) {
        fprintf(stderr, "Not enough memory to save completion data.\n");
        return 0;
    }
    memcpy(temporary_name, filename, filename_length);
    memcpy(temporary_name + filename_length, ".tmp", 5);

    /* Exclusive creation prevents replacing a pre-existing temporary file. */
    file = open_exclusive_temporary_file(temporary_name);
    if (!file) {
        fprintf(stderr, "Could not create temporary completion file '%s': %s. "
                        "Original file was not changed.\n",
                temporary_name, strerror(errno));
        free(temporary_name);
        return 0;
    }

    /* Keep existing valid rows in order, including identities absent from this catalog. */
    for (record = records; record; record = record->next) {
        int completed = record->matched_course
                      ? record->matched_course->completed : record->completed;
        if (!write_completion_record(file, record->semester, record->code, record->type,
                                     record->specialization, record->group, completed)) {
            write_failed = 1;
            break;
        }
    }
    for (course = head; !write_failed && course; course = course->next) {
        for (record = records; record; record = record->next)
            if (record->matched_course == course) break;
        if (!record && !write_completion_record(file, course->semester, course->code,
                                                course->type, course->specialization,
                                                course->group, course->completed))
            write_failed = 1;
    }

    if (fflush(file) != 0 || ferror(file)) write_failed = 1;
    if (fclose(file) != 0) write_failed = 1;
    if (write_failed) {
        fprintf(stderr, "Could not completely write temporary completion file '%s'. "
                        "Original file was not changed.\n", temporary_name);
        remove(temporary_name);
        free(temporary_name);
        return 0;
    }

#ifdef _WIN32
    if (!MoveFileExA(temporary_name, filename,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fprintf(stderr, "Could not safely replace '%s' (Windows error %lu). "
                        "Original file was not changed; staged data remains at '%s'.\n",
                filename, (unsigned long)GetLastError(), temporary_name);
        free(temporary_name);
        return 0;
    }
#else
    if (rename(temporary_name, filename) != 0) {
        fprintf(stderr, "Could not safely replace '%s': %s. "
                        "Original file was not changed; staged data remains at '%s'.\n",
                filename, strerror(errno), temporary_name);
        free(temporary_name);
        return 0;
    }
#endif

    free(temporary_name);
    return 1;
}

static void discard_rest_of_input_line(void)
{
    int character;
    while ((character = getchar()) != '\n' && character != EOF) { }
}

static int ask_completed(const Course *course, int *completed)
{
    char answer[32];
    for (;;) {
        size_t length;
        printf("Completed? (y/n, current %s): ", course->completed ? "yes" : "no");
        if (!fgets(answer, sizeof(answer), stdin)) {
            fprintf(stderr, "Input ended before a valid completion answer was entered.\n");
            return 0;
        }
        length = strlen(answer);
        if (!strchr(answer, '\n') && length == sizeof(answer) - 1 && !feof(stdin)) {
            discard_rest_of_input_line();
            printf("Please enter exactly y or n.\n");
            continue;
        }
        trim(answer);
        if (strcmp(answer, "y") == 0 || strcmp(answer, "Y") == 0) {
            *completed = 1;
            return 1;
        }
        if (strcmp(answer, "n") == 0 || strcmp(answer, "N") == 0) {
            *completed = 0;
            return 1;
        }
        printf("Please enter exactly y or n.\n");
    }
}

int track_courses_for_profile(Course *head, int semester,
                              const char *specialization, const char *group,
                              const char *status_file)
{
    Course *course;
    CompletionRecord *records = NULL;
    int main_credits = 0, elective_credits = 0;

    if (!load_completion_status(head, status_file, &records)) return 0;
    printf("\nMark each listed course completed or not completed.\n");
    for (course = head; course; course = course->next) {
        int completed;
        if (!profile_match(course, semester, specialization, group)) continue;
        printf("%s | %s | %d credits | %s\n", course->code, course->name,
               course->credits, course->type);
        if (!ask_completed(course, &completed)) {
            free_completion_records(records);
            return 0;
        }
        course->completed = completed;
        if (course->completed) {
            if (strcmp(course->type, "MAIN") == 0) main_credits += course->credits;
            else elective_credits += course->credits;
        }
    }
    printf("Completed compulsory credits: %d\n", main_credits);
    printf("Completed elective credits: %d (selected/completed options only)\n", elective_credits);
    if (!save_completion_status(head, status_file, records)) {
        free_completion_records(records);
        return 0;
    }
    printf("Completion data saved to %s\n", status_file);
    free_completion_records(records);
    return 1;
}

void show_courses_for_profile(const Course *head, int semester,
                              const char *specialization, const char *group)
{
    const Course *current = head;
    int found_main = 0;
    int found_elective = 0;
    int main_credits = 0;
    int elective_count = 0;

    printf("\n===============================================================\n");
    printf(" SEMESTER %d | SPECIALIZATION: %s", semester, specialization);
    if (semester == 1 || semester == 2) printf(" | GROUP: %s", group);
    printf("\n===============================================================\n");

    printf("\nMAIN / COMPULSORY COURSES\n");
    printf("%-12s | %-48s | %s\n", "Course code", "Course name", "Credits");
    printf("----------------------------------------------------------------\n");

    while (current != NULL) {
        int group_match = 1;
        if (semester == 1 || semester == 2) {
            group_match = (strcmp(current->group, "BOTH") == 0 ||
                           strcmp(current->group, group) == 0);
        }

        if (current->semester == semester &&
            specialization_matches(current->specialization, specialization) &&
            group_match && strcmp(current->type, "MAIN") == 0) {
            printf("%-12s | %-48s | %d\n",
                   current->code, current->name, current->credits);
            main_credits += current->credits;
            found_main = 1;
        }
        current = current->next;
    }

    if (!found_main) printf("No main courses matched this selection.\n");
    printf("Main-course credits listed: %d\n", main_credits);

    printf("\nELECTIVE OPTIONS (options are not all compulsory)\n");
    printf("%-12s | %-48s | %s\n", "Course code", "Course name", "Credits");
    printf("----------------------------------------------------------------\n");

    current = head;
    while (current != NULL) {
        int group_match = 1;
        if (semester == 1 || semester == 2) {
            group_match = (strcmp(current->group, "BOTH") == 0 ||
                           strcmp(current->group, group) == 0);
        }

        if (current->semester == semester &&
            specialization_matches(current->specialization, specialization) &&
            group_match && strcmp(current->type, "MAIN") != 0) {
            printf("%-12s | %-48s | %d (%s)\n",
                   current->code, current->name, current->credits, current->type);
            found_elective = 1;
            elective_count++;
        }
        current = current->next;
    }

    if (!found_elective) printf("No elective options are listed for this selection.\n");
    else printf("Elective options listed: %d (choose only as permitted by your curriculum)\n",
                elective_count);

    printf("\nNote: This is a draft catalog, not an official degree audit.\n");
}

void free_courses(Course *head)
{
    while (head != NULL) {
        Course *next = head->next;
        free(head);
        head = next;
    }
}
