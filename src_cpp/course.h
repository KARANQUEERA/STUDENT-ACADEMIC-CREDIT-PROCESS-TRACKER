#ifndef COURSE_H
#define COURSE_H

class Course {
private:
    int semester;
    char code[24];
    char name[160];
    int credits;
    char type[40];
    char specialization[100];
    char group[20];
    bool completed;

    static void copyText(char *destination, int capacity, const char *source);

public:
    Course();
    Course(int semester, const char *code, const char *name, int credits,
           const char *type, const char *specialization, const char *group);

    int getSemester() const;
    const char *getCode() const;
    const char *getName() const;
    int getCredits() const;
    const char *getType() const;
    const char *getSpecialization() const;
    const char *getGroup() const;
    bool isCompleted() const;
    void setCompleted(bool value);
    bool hasSameIdentity(const Course &other) const;
    void display() const;
};

class CourseCatalog {
private:
    struct Node {
        Course course;
        Node *next;
        Node(const Course &value) : course(value), next(0) {}
    };

    Node *head;
    int courseCount;
    int duplicateRows;

public:
    CourseCatalog();
    ~CourseCatalog();
    bool loadCsv(const char *filename);
    int size() const;
    int getDuplicateRowCount() const;
    Course *at(int index);
    const Course *at(int index) const;
    Course *findIdentity(const Course &course);
    const Course *findIdentity(const Course &course) const;
    int search(int semester, const char *specialization, const char *code,
               const char *type, const char *group, const char *name,
               int credits) const;
    bool matchesProfile(const Course &course, int semester,
                        const char *specialization, const char *group) const;
};

class CompletionStore {
private:
    struct Entry {
        int semester;
        char code[24];
        char type[40];
        char specialization[100];
        char group[20];
        bool completed;
        Entry *next;
        Entry();
    };

    Entry *head;
    bool loadValidated;
    void clearEntries();
    Entry *findEntry(const Course &course) const;
    Entry *addEntry(const Course &course);

public:
    CompletionStore();
    ~CompletionStore();
    bool load(const char *filename, CourseCatalog &catalog);
    bool save(const char *filename, const CourseCatalog &catalog);
};

class PrerequisiteGraph {
private:
    struct Edge {
        Course requiredCourse;
        Edge *next;
        explicit Edge(const Course &course);
    };
    struct Vertex {
        Course course;
        Edge *requirements;
        Vertex *next;
        explicit Vertex(const Course &value);
    };

    Vertex *vertices;
    int vertexCount;
    Vertex *findVertex(const Course &course) const;
    Vertex *addVertex(const Course &course);
    bool requirementsSatisfied(Vertex *vertex, int semester,
                              const CourseCatalog &catalog,
                              const Course **path, int depth) const;

public:
    enum Eligibility { DATA_UNAVAILABLE, PREREQUISITES_INCOMPLETE, ELIGIBLE };
    PrerequisiteGraph();
    ~PrerequisiteGraph();
    void addCourse(const Course &course);
    void addPrerequisite(const Course &course, const Course &requiredCourse);
    bool hasVerifiedData() const;
    Eligibility evaluate(const Course &course, const CourseCatalog &catalog) const;
};

class CoursePlanner {
private:
    static bool comesBefore(const Course *left, const Course *right);
    static void merge(const Course **items, const Course **scratch, int first,
                      int middle, int last);
    static void mergeSort(const Course **items, const Course **scratch,
                          int first, int last);

public:
    void showWhatIf(const CourseCatalog &catalog, const char *specialization,
                    const char *group, int semester,
                    const PrerequisiteGraph &prerequisites) const;
};

class AcademicProgress {
private:
    int earnedCompulsory;
    int markedElective;

public:
    AcademicProgress();
    void reportSemester(const CourseCatalog &catalog, const char *specialization,
                        const char *group, int semester);
    void reportTotals() const;
    void showCurrentSemester(const CourseCatalog &catalog,
                             const char *specialization, const char *group,
                             int semester) const;
};

int searchCoursesWithStatus(const CourseCatalog &catalog, const char *query,
                            int semester, const char *type,
                            const char *specialization, const char *group,
                            int completionFilter, const Course **results,
                            int capacity);
void printCompletedCreditReport(const CourseCatalog &catalog,
                                const char *specialization,
                                const char *group);

#endif
