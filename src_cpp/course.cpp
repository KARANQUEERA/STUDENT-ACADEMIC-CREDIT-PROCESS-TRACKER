#include "course.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cerrno>
#include <new>
#include <iostream>
#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

const int CSV_COLUMN_COUNT = 18;
const std::size_t CSV_RECORD_SIZE = 8192;
const char *CSV_HEADERS[CSV_COLUMN_COUNT] = {
    "semester", "code", "name", "credits", "type", "specialization", "group",
    "pre_requisite", "offered_by", "base_difficulty", "difficulty_level",
    "difficulty_reason", "key_background_areas", "syllabus_topics", "syllabus_source",
    "research_status", "difficulty_confidence", "elective_category"
};

void copyText(char *destination, int capacity, const char *source)
{
    if (capacity <= 0) return;
    if (source == 0) source = "";
    std::strncpy(destination, source, static_cast<std::size_t>(capacity - 1));
    destination[capacity - 1] = '\0';
}

void trim(char *text)
{
    int start = 0;
    int length = static_cast<int>(std::strlen(text));
    while (start < length && (text[start] == ' ' || text[start] == '\t' ||
           text[start] == '\r' || text[start] == '\n')) ++start;
    while (length > start && (text[length - 1] == ' ' || text[length - 1] == '\t' ||
           text[length - 1] == '\r' || text[length - 1] == '\n')) --length;
    if (start > 0) std::memmove(text, text + start, static_cast<std::size_t>(length - start));
    text[length - start] = '\0';
}

/* Read a complete logical CSV record, including quoted newlines. */
int readCsvRecord(std::FILE *file, char *record, std::size_t capacity)
{
    std::size_t used = 0;
    bool inQuotes = false;
    bool tooLong = false;
    int character;

    while ((character = std::fgetc(file)) != EOF) {
        if (character == '"') {
            if (inQuotes) {
                int following = std::fgetc(file);
                if (following == '"') {
                    if (used + 2 < capacity) {
                        record[used++] = '"';
                        record[used++] = '"';
                    } else {
                        tooLong = true;
                    }
                    continue;
                }
                inQuotes = false;
                if (following != EOF) std::ungetc(following, file);
            } else {
                inQuotes = true;
            }
        }
        else if (character == '\n' && !inQuotes) {
            if (used > 0 && record[used - 1] == '\r') --used;
            if (tooLong) return -1;
            record[used] = '\0';
            return 1;
        }

        if (used + 1 < capacity) record[used++] = static_cast<char>(character);
        else tooLong = true;
    }

    if (used == 0 && !tooLong) return 0;
    if (tooLong || inQuotes) return -1;
    record[used] = '\0';
    return 1;
}

/* Decode a CSV record in place; reject malformed quoting and excess columns. */
int parseCsvLine(char *line, char *fields[], int capacity)
{
    char *source = line;
    char *destination = line;
    int count = 0;

    for (;;) {
        bool hasComma;
        if (count >= capacity) return -1;
        fields[count++] = destination;
        if (*source == '"') {
            bool closed = false;
            ++source;
            while (*source) {
                if (*source == '"' && source[1] == '"') {
                    *destination++ = '"';
                    source += 2;
                } else if (*source == '"') {
                    ++source;
                    closed = true;
                    break;
                } else {
                    *destination++ = *source++;
                }
            }
            if (!closed || (*source != '\0' && *source != ',')) return -1;
        } else {
            while (*source && *source != ',') {
                if (*source == '"') return -1;
                *destination++ = *source++;
            }
        }

        hasComma = (*source == ',');
        if (hasComma) ++source;
        *destination++ = '\0';
        if (!hasComma) break;
    }
    return count;
}

bool validCsvHeader(char *fields[], int count)
{
    if (count != CSV_COLUMN_COUNT) return false;
    if (std::strlen(fields[0]) >= 3 &&
        static_cast<unsigned char>(fields[0][0]) == 0xef &&
        static_cast<unsigned char>(fields[0][1]) == 0xbb &&
        static_cast<unsigned char>(fields[0][2]) == 0xbf)
        std::memmove(fields[0], fields[0] + 3, std::strlen(fields[0] + 3) + 1);
    for (int column = 0; column < CSV_COLUMN_COUNT; ++column)
        if (std::strcmp(fields[column], CSV_HEADERS[column]) != 0) return false;
    return true;
}

bool parseInteger(const char *text, long minimum, long maximum, int &value)
{
    char *end;
    long parsed;
    errno = 0;
    parsed = std::strtol(text, &end, 10);
    if (text[0] == '\0' || *end != '\0' || errno == ERANGE ||
        parsed < minimum || parsed > maximum) return false;
    value = static_cast<int>(parsed);
    return true;
}

bool specializationMatches(const char *catalogValue, const char *wanted)
{
    const char *start = catalogValue;
    if (wanted == 0 || wanted[0] == '\0') return true;
    while (*start) {
        const char *end = std::strchr(start, '|');
        std::size_t length = end ? static_cast<std::size_t>(end - start)
                                 : std::strlen(start);
        if ((length == 3 && std::strncmp(start, "ALL", length) == 0) ||
            (std::strlen(wanted) == length && std::strncmp(start, wanted, length) == 0))
            return true;
        if (!end) break;
        start = end + 1;
    }
    return false;
}

bool optionalEquals(const char *value, const char *filter)
{
    return filter == 0 || filter[0] == '\0' || std::strcmp(value, filter) == 0;
}

bool containsIgnoreCase(const char *value, const char *query)
{
    if (query == 0 || query[0] == '\0') return true;
    for (const char *start = value; *start; ++start) {
        const char *left = start;
        const char *right = query;
        while (*left && *right &&
               std::tolower(static_cast<unsigned char>(*left)) ==
               std::tolower(static_cast<unsigned char>(*right))) {
            ++left;
            ++right;
        }
        if (*right == '\0') return true;
    }
    return false;
}

int splitPipeFields(char *line, char *fields[], int capacity)
{
    int count = 1;
    fields[0] = line;
    for (char *cursor = line; *cursor; ++cursor) {
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

bool readWhatIfCount(int maximum, int &chosen)
{
    char line[64];
    for (;;) {
        char *end;
        long value;
        std::cout << "Include how many top-ranked options in a temporary what-if list? (0-"
                  << maximum << ", 0 cancels): ";
        if (!std::fgets(line, sizeof(line), stdin)) return false;
        value = std::strtol(line, &end, 10);
        while (*end && std::isspace(static_cast<unsigned char>(*end))) ++end;
        if (end != line && *end == '\0' && value >= 0 && value <= maximum) {
            chosen = static_cast<int>(value);
            return true;
        }
        std::cout << "Enter a whole number from 0 to " << maximum << ".\n";
    }
}

std::FILE *openExclusiveFile(const char *filename)
{
    int descriptor;
#ifdef _WIN32
    descriptor = _open(filename, _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY,
                       _S_IREAD | _S_IWRITE);
    if (descriptor < 0) return 0;
    std::FILE *file = _fdopen(descriptor, "wb");
    if (!file) {
        _close(descriptor);
        std::remove(filename);
    }
#else
    descriptor = open(filename, O_CREAT | O_EXCL | O_WRONLY, 0600);
    if (descriptor < 0) return 0;
    std::FILE *file = fdopen(descriptor, "wb");
    if (!file) {
        close(descriptor);
        std::remove(filename);
    }
#endif
    return file;
}

bool replaceFile(const char *temporaryName, const char *filename)
{
#ifdef _WIN32
    return MoveFileExA(temporaryName, filename,
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return std::rename(temporaryName, filename) == 0;
#endif
}

} // namespace

void Course::copyText(char *destination, int capacity, const char *source)
{
    ::copyText(destination, capacity, source);
}

Course::Course() : semester(0), credits(0), completed(false)
{
    code[0] = name[0] = type[0] = specialization[0] = group[0] = '\0';
}

Course::Course(int courseSemester, const char *courseCode, const char *courseName,
               int courseCredits, const char *courseType,
               const char *courseSpecialization, const char *courseGroup)
    : semester(courseSemester), credits(courseCredits), completed(false)
{
    copyText(code, sizeof(code), courseCode);
    copyText(name, sizeof(name), courseName);
    copyText(type, sizeof(type), courseType);
    copyText(specialization, sizeof(specialization), courseSpecialization);
    copyText(group, sizeof(group), courseGroup);
}

int Course::getSemester() const { return semester; }
const char *Course::getCode() const { return code; }
const char *Course::getName() const { return name; }
int Course::getCredits() const { return credits; }
const char *Course::getType() const { return type; }
const char *Course::getSpecialization() const { return specialization; }
const char *Course::getGroup() const { return group; }
bool Course::isCompleted() const { return completed; }
void Course::setCompleted(bool value) { completed = value; }

bool Course::hasSameIdentity(const Course &other) const
{
    return semester == other.semester && std::strcmp(code, other.code) == 0 &&
           std::strcmp(type, other.type) == 0 &&
           std::strcmp(specialization, other.specialization) == 0 &&
           std::strcmp(group, other.group) == 0;
}

void Course::display() const
{
    std::cout << code << " - " << name << " (Semester " << semester << ", "
              << credits << " credits, " << type << ", specialization: "
              << specialization << ", group: " << group << ")";
}

CourseCatalog::CourseCatalog() : head(0), courseCount(0), duplicateRows(0) {}

CourseCatalog::~CourseCatalog()
{
    while (head) {
        Node *next = head->next;
        delete head;
        head = next;
    }
}

bool CourseCatalog::loadCsv(const char *filename)
{
    std::FILE *file = std::fopen(filename, "r");
    char line[CSV_RECORD_SIZE];
    char *fields[CSV_COLUMN_COUNT];
    int recordStatus;
    int loaded = 0;
    if (!file) return false;
    recordStatus = readCsvRecord(file, line, sizeof(line));
    if (recordStatus != 1 || !validCsvHeader(fields,
            parseCsvLine(line, fields, CSV_COLUMN_COUNT))) {
        std::fclose(file);
        return false;
    }

    while ((recordStatus = readCsvRecord(file, line, sizeof(line))) == 1) {
        Course course;
        Node *node;
        bool duplicate = false;
        int count;
        int semester;
        int credits;

        if (line[0] == '\0') continue;
        count = parseCsvLine(line, fields, CSV_COLUMN_COUNT);
        if (count != CSV_COLUMN_COUNT) {
            std::fclose(file);
            return false;
        }
        for (int field = 0; field < 7; ++field) trim(fields[field]);
        if (!parseInteger(fields[0], 1, 8, semester) ||
            !parseInteger(fields[3], 0, 1000, credits) ||
            fields[1][0] == '\0' || fields[2][0] == '\0' ||
            fields[4][0] == '\0' || fields[5][0] == '\0' ||
            std::strlen(fields[1]) >= 24 || std::strlen(fields[2]) >= 160 ||
            std::strlen(fields[4]) >= 40 || std::strlen(fields[5]) >= 100 ||
            std::strlen(fields[6]) >= 20) {
            std::fclose(file);
            return false;
        }
        course = Course(semester, fields[1], fields[2], credits,
                        fields[4], fields[5], fields[6]);
        for (node = head; node; node = node->next) {
            if (node->course.hasSameIdentity(course)) { duplicate = true; break; }
        }
        if (duplicate) { ++duplicateRows; continue; }
        node = new (std::nothrow) Node(course);
        if (!node) { std::fclose(file); return false; }
        if (!head) head = node;
        else {
            Node *tail = head;
            while (tail->next) tail = tail->next;
            tail->next = node;
        }
        ++courseCount;
        ++loaded;
    }
    std::fclose(file);
    if (recordStatus < 0) return false;
    return loaded > 0;
}

int CourseCatalog::size() const { return courseCount; }
int CourseCatalog::getDuplicateRowCount() const { return duplicateRows; }

Course *CourseCatalog::at(int index)
{
    Node *node = head;
    while (node && index > 0) { node = node->next; --index; }
    return node ? &node->course : 0;
}

const Course *CourseCatalog::at(int index) const
{
    const Node *node = head;
    while (node && index > 0) { node = node->next; --index; }
    return node ? &node->course : 0;
}

Course *CourseCatalog::findIdentity(const Course &course)
{
    Node *node;
    for (node = head; node; node = node->next)
        if (node->course.hasSameIdentity(course)) return &node->course;
    return 0;
}

const Course *CourseCatalog::findIdentity(const Course &course) const
{
    const Node *node;
    for (node = head; node; node = node->next)
        if (node->course.hasSameIdentity(course)) return &node->course;
    return 0;
}

bool CourseCatalog::matchesProfile(const Course &course, int semester,
                                   const char *specialization, const char *group) const
{
    bool groupMatches = semester > 2 || group == 0 || group[0] == '\0' ||
                        std::strcmp(course.getGroup(), "BOTH") == 0 ||
                        std::strcmp(course.getGroup(), group) == 0;
    return course.getSemester() == semester &&
           specializationMatches(course.getSpecialization(), specialization) &&
           groupMatches;
}

int CourseCatalog::search(int semester, const char *specialization, const char *code,
                          const char *type, const char *group,
                          const char *name, int credits) const
{
    int found = 0;
    int index;
    for (index = 0; index < courseCount; ++index) {
        const Course *course = at(index);
        if ((semester == 0 || semester == course->getSemester()) &&
            specializationMatches(course->getSpecialization(), specialization) &&
            optionalEquals(course->getCode(), code) &&
            optionalEquals(course->getType(), type) &&
            containsIgnoreCase(course->getName(), name) &&
            (credits == 0 || course->getCredits() == credits) &&
            (course->getSemester() > 2 || optionalEquals(course->getGroup(), group) ||
             std::strcmp(course->getGroup(), "BOTH") == 0)) {
            course->display();
            std::cout << "\n";
            ++found;
        }
    }
    if (found == 0) std::cout << "No catalog courses matched those filters.\n";
    return found;
}

CompletionStore::Entry::Entry() : semester(0), completed(false), next(0)
{
    code[0] = type[0] = specialization[0] = group[0] = '\0';
}

CompletionStore::CompletionStore() : head(0), loadValidated(false) {}

CompletionStore::~CompletionStore()
{
    clearEntries();
}

void CompletionStore::clearEntries()
{
    while (head) {
        Entry *next = head->next;
        delete head;
        head = next;
    }
}

CompletionStore::Entry *CompletionStore::findEntry(const Course &course) const
{
    Entry *entry;
    for (entry = head; entry; entry = entry->next) {
        if (entry->semester == course.getSemester() &&
            std::strcmp(entry->code, course.getCode()) == 0 &&
            std::strcmp(entry->type, course.getType()) == 0 &&
            std::strcmp(entry->specialization, course.getSpecialization()) == 0 &&
            std::strcmp(entry->group, course.getGroup()) == 0) return entry;
    }
    return 0;
}

CompletionStore::Entry *CompletionStore::addEntry(const Course &course)
{
    Entry *entry = new (std::nothrow) Entry;
    Entry *tail;
    if (!entry) return 0;
    entry->semester = course.getSemester();
    copyText(entry->code, sizeof(entry->code), course.getCode());
    copyText(entry->type, sizeof(entry->type), course.getType());
    copyText(entry->specialization, sizeof(entry->specialization), course.getSpecialization());
    copyText(entry->group, sizeof(entry->group), course.getGroup());
    if (!head) head = entry;
    else {
        tail = head;
        while (tail->next) tail = tail->next;
        tail->next = entry;
    }
    return entry;
}

bool CompletionStore::load(const char *filename, CourseCatalog &catalog)
{
    std::FILE *file;
    char line[512];
    char *fields[32];
    int records = 0;
    int lineNumber = 0;
    bool failed = false;

    clearEntries();
    loadValidated = false;
    errno = 0;
    file = std::fopen(filename, "r");
    if (!file) {
        int openError = errno;
        if (openError == ENOENT) {
            std::cout << "No saved completion file exists; starting with all courses incomplete.\n";
            for (int index = 0; index < catalog.size(); ++index)
                catalog.at(index)->setCompleted(false);
            loadValidated = true;
            return true;
        }
        std::cerr << "Could not open completion file '" << filename << "': "
                  << std::strerror(openError) << ". No changes were saved.\n";
        return false;
    }

    while (std::fgets(line, sizeof(line), file)) {
        Entry values;
        int fieldCount;
        Entry *existing;
        int semesterValue;
        int completedValue;
        bool invalidSpecialization = false;

        ++lineNumber;
        if (!std::strchr(line, '\n') && std::strlen(line) == sizeof(line) - 1
            && !std::feof(file)) {
            std::cerr << "Completion record on line " << lineNumber
                      << " is too long. File was not changed.\n";
            failed = true;
            break;
        }
        fieldCount = splitPipeFields(line, fields, 32);
        if (fieldCount < 6 || fieldCount > 32) {
            std::cerr << "Malformed completion record on line " << lineNumber
                      << ". File was not changed.\n";
            failed = true;
            break;
        }
        for (int part = 0; part < fieldCount; ++part) trim(fields[part]);

        if (!parseInteger(fields[0], 1, 8, semesterValue) ||
            !parseInteger(fields[fieldCount - 1], 0, 1, completedValue) ||
            fields[1][0] == '\0' || fields[2][0] == '\0' ||
            fields[fieldCount - 2][0] == '\0' ||
            std::strlen(fields[1]) >= sizeof(values.code) ||
            std::strlen(fields[2]) >= sizeof(values.type) ||
            std::strlen(fields[fieldCount - 2]) >= sizeof(values.group)) {
            std::cerr << "Invalid identity, number, or status on completion line "
                      << lineNumber << ". File was not changed.\n";
            failed = true;
            break;
        }
        if (std::strcmp(fields[2], "MAIN") != 0 &&
            std::strcmp(fields[2], "ELECTIVE") != 0) {
            std::cerr << "Invalid course type on completion line " << lineNumber
                      << ". File was not changed.\n";
            failed = true;
            break;
        }
        values.semester = static_cast<int>(semesterValue);
        copyText(values.code, sizeof(values.code), fields[1]);
        copyText(values.type, sizeof(values.type), fields[2]);
        copyText(values.group, sizeof(values.group), fields[fieldCount - 2]);
        values.specialization[0] = '\0';
        for (int part = 3; part < fieldCount - 2; ++part) {
            std::size_t used = std::strlen(values.specialization);
            std::size_t add = std::strlen(fields[part]);
            if (add == 0 || used + add + (part > 3 ? 1U : 0U) >=
                            sizeof(values.specialization)) {
                invalidSpecialization = true;
                break;
            }
            if (part > 3) std::strcat(values.specialization, "|");
            std::strcat(values.specialization, fields[part]);
        }
        if (invalidSpecialization || values.specialization[0] == '\0') {
            std::cerr << "Invalid specialization on completion line " << lineNumber
                      << ". File was not changed.\n";
            failed = true;
            break;
        }
        values.completed = completedValue == 1;
        Course identity(values.semester, values.code, "", 0, values.type,
                        values.specialization, values.group);
        existing = findEntry(identity);
        if (existing) {
            std::cerr << "Duplicate course identity on completion line " << lineNumber
                      << ". File was not changed.\n";
            failed = true;
            break;
        }
        existing = addEntry(identity);
        if (!existing) {
            std::cerr << "Not enough memory to read completion data. File was not changed.\n";
            failed = true;
            break;
        }
        existing->completed = values.completed;
        ++records;
    }

    if (std::ferror(file)) {
        std::cerr << "Error reading completion file '" << filename
                  << "'. File was not changed.\n";
        failed = true;
    }
    if (std::fclose(file) != 0) {
        std::cerr << "Error closing completion file '" << filename
                  << "'. File was not changed.\n";
        failed = true;
    }
    if (records == 0 && lineNumber == 0 && !failed) {
        std::cerr << "Completion file '" << filename
                  << "' is empty. File was not changed.\n";
        failed = true;
    }
    if (failed) {
        clearEntries();
        return false;
    }

    for (int index = 0; index < catalog.size(); ++index)
        catalog.at(index)->setCompleted(false);
    for (int index = 0; index < catalog.size(); ++index) {
        Course *course = catalog.at(index);
        Entry *entry = findEntry(*course);
        if (entry) course->setCompleted(entry->completed);
    }
    loadValidated = true;
    return true;
}

bool CompletionStore::save(const char *filename, const CourseCatalog &catalog)
{
    std::FILE *file;
    Entry *entry;
    int index;
    char temporaryName[1024];
    bool writeFailed = false;

    if (!loadValidated) {
        std::cerr << "Refusing to save completion data because no valid load completed.\n";
        return false;
    }

    for (index = 0; index < catalog.size(); ++index) {
        const Course *course = catalog.at(index);
        Entry *record = findEntry(*course);
        if (!record) record = addEntry(*course);
        if (!record) {
            std::cerr << "Not enough memory to prepare completion data for saving.\n";
            return false;
        }
        record->completed = course->isCompleted();
    }

    if (std::strlen(filename) + 5 >= sizeof(temporaryName)) {
        std::cerr << "Completion file path is too long; data was not saved.\n";
        return false;
    }
    std::strcpy(temporaryName, filename);
    std::strcat(temporaryName, ".tmp");
    file = openExclusiveFile(temporaryName);
    if (!file) {
        std::cerr << "Could not create temporary completion file '" << temporaryName
                  << "': " << std::strerror(errno)
                  << ". Original file was not changed.\n";
        return false;
    }

    for (entry = head; entry; entry = entry->next) {
        if (std::fprintf(file, "%d|%s|%s|%s|%s|%d\n", entry->semester,
                         entry->code, entry->type, entry->specialization,
                         entry->group, entry->completed ? 1 : 0) < 0) {
            writeFailed = true;
            break;
        }
    }
    if (std::fflush(file) != 0 || std::ferror(file)) writeFailed = true;
    if (std::fclose(file) != 0) writeFailed = true;
    if (writeFailed) {
        std::cerr << "Could not completely write staged completion file '"
                  << temporaryName << "'. Original file was not changed.\n";
        std::remove(temporaryName);
        return false;
    }

    if (!replaceFile(temporaryName, filename)) {
#ifdef _WIN32
        std::cerr << "Could not safely replace completion file '" << filename
                  << "' (Windows error " << static_cast<unsigned long>(GetLastError())
                  << "). Original file was not changed; staged data remains at '"
                  << temporaryName << "'.\n";
#else
        std::cerr << "Could not safely replace completion file '" << filename
                  << "': " << std::strerror(errno)
                  << ". Original file was not changed; staged data remains at '"
                  << temporaryName << "'.\n";
#endif
        return false;
    }
    return true;
}

PrerequisiteGraph::Edge::Edge(const Course &course) : requiredCourse(course), next(0) {}

PrerequisiteGraph::Vertex::Vertex(const Course &value)
    : course(value), requirements(0), next(0) {}

PrerequisiteGraph::PrerequisiteGraph() : vertices(0), vertexCount(0) {}

PrerequisiteGraph::~PrerequisiteGraph()
{
    while (vertices) {
        Vertex *nextVertex = vertices->next;
        while (vertices->requirements) {
            Edge *nextEdge = vertices->requirements->next;
            delete vertices->requirements;
            vertices->requirements = nextEdge;
        }
        delete vertices;
        vertices = nextVertex;
    }
}

PrerequisiteGraph::Vertex *PrerequisiteGraph::findVertex(const Course &course) const
{
    Vertex *vertex;
    for (vertex = vertices; vertex; vertex = vertex->next)
        if (vertex->course.hasSameIdentity(course)) return vertex;
    return 0;
}

PrerequisiteGraph::Vertex *PrerequisiteGraph::addVertex(const Course &course)
{
    Vertex *vertex = findVertex(course);
    Vertex *tail;
    if (vertex) return vertex;
    vertex = new (std::nothrow) Vertex(course);
    if (!vertex) return 0;
    if (!vertices) vertices = vertex;
    else {
        tail = vertices;
        while (tail->next) tail = tail->next;
        tail->next = vertex;
    }
    ++vertexCount;
    return vertex;
}

void PrerequisiteGraph::addCourse(const Course &course) { addVertex(course); }

void PrerequisiteGraph::addPrerequisite(const Course &course,
                                       const Course &requiredCourse)
{
    Vertex *vertex = addVertex(course);
    Edge *edge;
    if (!vertex) return;
    addVertex(requiredCourse);
    for (edge = vertex->requirements; edge; edge = edge->next)
        if (edge->requiredCourse.hasSameIdentity(requiredCourse)) return;
    edge = new (std::nothrow) Edge(requiredCourse);
    if (!edge) return;
    edge->next = vertex->requirements;
    vertex->requirements = edge;
}

bool PrerequisiteGraph::hasVerifiedData() const { return vertices != 0; }

bool PrerequisiteGraph::requirementsSatisfied(Vertex *vertex, int semester,
                                             const CourseCatalog &catalog,
                                             const Course **path, int depth) const
{
    Edge *edge;
    /* Track the current DFS path so a cycle cannot recurse forever. */
    if (depth > vertexCount) return false;
    for (int index = 0; index < depth; ++index)
        if (path[index]->hasSameIdentity(vertex->course)) return false;
    path[depth] = &vertex->course;
    for (edge = vertex->requirements; edge; edge = edge->next) {
        bool foundCompleted = false;
        for (int index = 0; index < catalog.size(); ++index) {
            const Course *candidate = catalog.at(index);
            if (candidate->hasSameIdentity(edge->requiredCourse) &&
                candidate->getSemester() < semester && candidate->isCompleted()) {
                foundCompleted = true;
                break;
            }
        }
        if (!foundCompleted) return false;
        Vertex *required = findVertex(edge->requiredCourse);
        if (required && required->requirements &&
            !requirementsSatisfied(required, semester, catalog, path, depth + 1))
            return false;
    }
    return true;
}

PrerequisiteGraph::Eligibility PrerequisiteGraph::evaluate(
    const Course &course, const CourseCatalog &catalog) const
{
    Vertex *vertex = findVertex(course);
    const Course **path;
    bool satisfied;
    if (!vertex) return DATA_UNAVAILABLE;
    path = new (std::nothrow) const Course*[vertexCount + 1];
    if (!path) return PREREQUISITES_INCOMPLETE;
    satisfied = requirementsSatisfied(vertex, course.getSemester(), catalog, path, 0);
    delete [] path;
    return satisfied ? ELIGIBLE : PREREQUISITES_INCOMPLETE;
}

bool CoursePlanner::comesBefore(const Course *left, const Course *right)
{
    int leftMain = std::strcmp(left->getType(), "MAIN") == 0 ? 0 : 1;
    int rightMain = std::strcmp(right->getType(), "MAIN") == 0 ? 0 : 1;
    if (leftMain != rightMain) return leftMain < rightMain;
    if (left->getCredits() != right->getCredits())
        return left->getCredits() > right->getCredits();
    return std::strcmp(left->getCode(), right->getCode()) < 0;
}

void CoursePlanner::merge(const Course **items, const Course **scratch, int first,
                          int middle, int last)
{
    int left = first, right = middle, out = first;
    /* Merge the sorted halves into scratch, then copy them back. */
    while (left < middle && right < last) {
        if (comesBefore(items[left], items[right])) scratch[out++] = items[left++];
        else scratch[out++] = items[right++];
    }
    while (left < middle) scratch[out++] = items[left++];
    while (right < last) scratch[out++] = items[right++];
    for (left = first; left < last; ++left) items[left] = scratch[left];
}

void CoursePlanner::mergeSort(const Course **items, const Course **scratch, int first, int last)
{
    int middle;
    if (last - first < 2) return;
    middle = first + (last - first) / 2;
    mergeSort(items, scratch, first, middle);
    mergeSort(items, scratch, middle, last);
    merge(items, scratch, first, middle, last);
}

void CoursePlanner::showWhatIf(const CourseCatalog &catalog,
                               const char *specialization, const char *group,
                               int semester,
                               const PrerequisiteGraph &prerequisites) const
{
    const Course **items = new (std::nothrow) const Course*[catalog.size()];
    const Course **scratch = new (std::nothrow) const Course*[catalog.size()];
    int count = 0;
    int index;
    if (!items || !scratch) {
        delete [] items; delete [] scratch;
        std::cout << "Not enough memory for the planning view.\n";
        return;
    }
    for (index = 0; index < catalog.size(); ++index) {
        const Course *course = catalog.at(index);
        if (!course->isCompleted() && catalog.matchesProfile(*course, semester,
                                                              specialization, group))
            items[count++] = course;
    }
    mergeSort(items, scratch, 0, count);
    std::cout << "\nRead-only what-if ranking (MAIN first, then more credits, then code):\n";
    if (!prerequisites.hasVerifiedData())
        std::cout << "Prerequisite eligibility is unavailable: the catalog has no prerequisite data.\n";
    for (index = 0; index < count; ++index) {
        const Course *course = items[index];
        PrerequisiteGraph::Eligibility eligibility;
        std::cout << index + 1 << ". ";
        course->display();
        eligibility = prerequisites.evaluate(*course, catalog);
        if (eligibility == PrerequisiteGraph::DATA_UNAVAILABLE)
            std::cout << " | eligibility not verified (prerequisite data missing)";
        else if (eligibility == PrerequisiteGraph::PREREQUISITES_INCOMPLETE)
            std::cout << " | prerequisites incomplete";
        else
            std::cout << " | listed prerequisites satisfied";
        if (std::strcmp(course->getType(), "ELECTIVE") == 0)
            std::cout << " | elective group not identified in catalog";
        std::cout << "\n";
    }
    if (count == 0) std::cout << "No uncompleted courses are listed for this semester.\n";
    if (count > 0) {
        int chosen = 0;
        int hypotheticalMain = 0, hypotheticalElective = 0;
        if (readWhatIfCount(count, chosen) && chosen > 0) {
            for (index = 0; index < chosen; ++index) {
                if (std::strcmp(items[index]->getType(), "MAIN") == 0)
                    hypotheticalMain += items[index]->getCredits();
                else if (std::strcmp(items[index]->getType(), "ELECTIVE") == 0)
                    hypotheticalElective += items[index]->getCredits();
            }
            std::cout << "Temporary what-if load: " << hypotheticalMain
                      << " compulsory credits and " << hypotheticalElective
                      << " elective-option credits.\n";
            std::cout << "The elective portion has unverified grouping and is not counted as earned.\n";
        } else {
            std::cout << "No what-if courses were selected.\n";
        }
    }
    std::cout << "This planning view does not change saved completion statuses.\n";
    delete [] items;
    delete [] scratch;
}

AcademicProgress::AcademicProgress() : earnedCompulsory(0), markedElective(0) {}

void AcademicProgress::reportSemester(const CourseCatalog &catalog,
                                      const char *specialization, const char *group,
                                      int semester)
{
    int offeredMain = 0, offeredElective = 0;
    int earnedMain = 0, completedElective = 0;
    for (int index = 0; index < catalog.size(); ++index) {
        const Course *course = catalog.at(index);
        if (!catalog.matchesProfile(*course, semester, specialization, group)) continue;
        if (std::strcmp(course->getType(), "MAIN") == 0) {
            offeredMain += course->getCredits();
            if (course->isCompleted()) earnedMain += course->getCredits();
        } else if (std::strcmp(course->getType(), "ELECTIVE") == 0) {
            offeredElective += course->getCredits();
            if (course->isCompleted()) completedElective += course->getCredits();
        }
    }
    earnedCompulsory += earnedMain;
    markedElective += completedElective;
    std::cout << "Semester " << semester << " history: compulsory earned " << earnedMain
              << " / " << offeredMain << " offered credits; elective options marked complete "
              << completedElective << " / " << offeredElective << " offered credits.\n";
}

void AcademicProgress::reportTotals() const
{
    std::cout << "Cumulative verified compulsory credits earned: " << earnedCompulsory << "\n";
    std::cout << "Completed elective option credits (unverified grouping): " << markedElective << "\n";
    std::cout << "Elective credits are excluded from verified earned total until an elective-group field exists.\n";
    std::cout << "Remaining graduation credits: unavailable (no verified target total is present).\n";
}

void AcademicProgress::showCurrentSemester(const CourseCatalog &catalog,
                                          const char *specialization,
                                          const char *group, int semester) const
{
    int offeredMain = 0, offeredElective = 0;
    std::cout << "\nCURRENT SEMESTER " << semester << " COURSES (offered, not automatically earned)\n";
    for (int index = 0; index < catalog.size(); ++index) {
        const Course *course = catalog.at(index);
        if (!catalog.matchesProfile(*course, semester, specialization, group)) continue;
        course->display();
        std::cout << " | saved status: " << (course->isCompleted() ? "complete" : "incomplete") << "\n";
        if (std::strcmp(course->getType(), "MAIN") == 0) offeredMain += course->getCredits();
        else if (std::strcmp(course->getType(), "ELECTIVE") == 0) offeredElective += course->getCredits();
    }
    std::cout << "Offered this semester: " << offeredMain << " compulsory credits and "
              << offeredElective << " elective-option credits. These are not earned totals.\n";
}
