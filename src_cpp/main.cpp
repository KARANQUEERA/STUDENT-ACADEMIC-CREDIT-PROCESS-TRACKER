#include "course.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

bool readLine(const char *prompt, char *buffer, int capacity)
{
    std::cout << prompt;
    if (!std::fgets(buffer, capacity, stdin)) return false;
    std::size_t length = std::strlen(buffer);
    if (length > 0 && buffer[length - 1] == '\n') buffer[length - 1] = '\0';
    else {
        int character;
        while ((character = std::getchar()) != '\n' && character != EOF) { }
    }
    return true;
}

bool readNumber(const char *prompt, int minimum, int maximum, int &result)
{
    char line[128];
    for (;;) {
        char *end;
        long value;
        if (!readLine(prompt, line, sizeof(line))) return false;
        value = std::strtol(line, &end, 10);
        while (*end == ' ' || *end == '\t' || *end == '\r') ++end;
        if (end != line && *end == '\0' && value >= minimum && value <= maximum) {
            result = static_cast<int>(value);
            return true;
        }
        std::cout << "Enter a whole number from " << minimum << " to " << maximum << ".\n";
    }
}

bool readYesNoKeep(const Course &course, bool &answer)
{
    char line[64];
    for (;;) {
        std::cout << "Completed? (y/n, Enter keeps current "
                  << (course.isCompleted() ? "yes" : "no") << "): ";
        if (!readLine("", line, sizeof(line))) return false;
        std::size_t length = std::strlen(line);
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r' ||
                              line[length - 1] == ' ' || line[length - 1] == '\t'))
            line[--length] = '\0';
        if (length == 0) { answer = course.isCompleted(); return true; }
        if (length == 1 && (line[0] == 'y' || line[0] == 'Y')) {
            answer = true; return true;
        }
        if (length == 1 && (line[0] == 'n' || line[0] == 'N')) {
            answer = false; return true;
        }
        std::cout << "Please enter y, n, or press Enter to keep the saved value.\n";
    }
}

bool updateSemester(CourseCatalog &catalog, const char *specialization,
                    const char *group, int semester)
{
    bool found = false;
    for (int index = 0; index < catalog.size(); ++index) {
        Course *course = catalog.at(index);
        bool completed;
        if (!catalog.matchesProfile(*course, semester, specialization, group)) continue;
        found = true;
        course->display();
        std::cout << "\n";
        if (!readYesNoKeep(*course, completed)) return false;
        course->setCompleted(completed);
    }
    if (!found) std::cout << "No courses matched this semester and profile.\n";
    return true;
}

bool askYesNo(const char *prompt, bool &answer)
{
    char line[32];
    for (;;) {
        std::size_t length;
        if (!readLine(prompt, line, sizeof(line))) return false;
        length = std::strlen(line);
        while (length > 0 && (line[length - 1] == ' ' || line[length - 1] == '\t'))
            line[--length] = '\0';
        if ((length == 1 && (line[0] == 'y' || line[0] == 'Y')) ||
            (length == 3 && (line[0] == 'y' || line[0] == 'Y') &&
             (line[1] == 'e' || line[1] == 'E') && (line[2] == 's' || line[2] == 'S'))) {
            answer = true; return true;
        }
        if ((length == 1 && (line[0] == 'n' || line[0] == 'N')) ||
            (length == 2 && (line[0] == 'n' || line[0] == 'N') &&
             (line[1] == 'o' || line[1] == 'O'))) {
            answer = false; return true;
        }
        std::cout << "Please enter y or n.\n";
    }
}

} // namespace

int main()
{
    const char *specializations[] = {"CORE", "AIML", "DS", "CS"};
    const char *specializationNames[] = {"CSE Core", "AI/ML", "AI and Data Science",
                                         "Cyber Security"};
    CourseCatalog catalog;
    CompletionStore completions;
    AcademicProgress progress;
    PrerequisiteGraph prerequisites;
    CoursePlanner planner;
    const char *group = "BOTH";
    int specializationChoice, groupChoice, currentSemester;
    bool answer;

    std::cout << "STUDENT ACADEMIC CREDIT PROCESS TRACKER - C++ COMPONENTS\n";
    std::cout << "Catalog: data/course_catalog_final_final_final.csv\n";
    if (!catalog.loadCsv("data/course_catalog_final_final_final.csv")) {
        std::cerr << "Could not load the catalog or its headers do not match the expected fields.\n";
        return 1;
    }
    if (catalog.getDuplicateRowCount() > 0)
        std::cout << "Skipped exact duplicate catalog rows: "
                  << catalog.getDuplicateRowCount() << "\n";
    std::cout << "Loaded " << catalog.size() << " catalog rows.\n";

    std::cout << "\nChoose specialization:\n";
    for (int index = 0; index < 4; ++index)
        std::cout << index + 1 << ". " << specializationNames[index] << "\n";
    if (!readNumber("Choice (1-4): ", 1, 4, specializationChoice) ||
        !readNumber("Current semester (1-8): ", 1, 8, currentSemester)) {
        std::cout << "Input ended before a profile was selected.\n";
        return 1;
    }
    if (!readNumber("First-year group (1 Chemistry, 2 Physics): ", 1, 2, groupChoice)) {
        std::cout << "Input ended before a group was selected.\n";
        return 1;
    }
    group = groupChoice == 1 ? "CHEMISTRY" : "PHYSICS";
    const char *specialization = specializations[specializationChoice - 1];

    if (!completions.load("data/course_completion.csv", catalog)) {
        std::cerr << "Could not read valid saved completion rows; stopping to protect the existing file.\n";
        return 1;
    }

    std::cout << "\nPREVIOUS SEMESTER HISTORY (Semesters 1 through "
              << currentSemester - 1 << ")\n";
    if (currentSemester == 1) {
        std::cout << "No previous-semester history is requested for Semester 1.\n";
    } else {
        for (int semester = 1; semester < currentSemester; ++semester) {
            std::cout << "\nUpdate/review Semester " << semester
                      << " (press Enter to keep each saved status):\n";
            if (!updateSemester(catalog, specialization, group, semester)) {
                std::cout << "Input ended while reviewing history; no changes were saved.\n";
                return 1;
            }
            progress.reportSemester(catalog, specialization, group, semester);
        }
    }
    progress.reportTotals();

    progress.showCurrentSemester(catalog, specialization, group, currentSemester);
    std::cout << "\nCurrent-semester statuses are separate from prior earned-credit totals.\n";
    std::cout << "The catalog has no elective_group field; elective group conflicts cannot be validated.\n";
    std::cout << "The catalog has no prerequisite data; no course is declared officially eligible.\n";
    if (!askYesNo("Review/update current-semester statuses (y/n)? ", answer)) return 1;
    if (answer && !updateSemester(catalog, specialization, group, currentSemester)) {
        std::cout << "Input ended while reviewing the current semester; no changes were saved.\n";
        return 1;
    }

    bool search;
    if (!askYesNo("Search the catalog with optional filters (y/n)? ", search)) return 1;
    if (search) {
        int searchSemester, searchType, searchGroup, searchCredits;
        char code[64];
        char name[160];
        const char *typeFilter = 0;
        const char *groupFilter = 0;
        if (!readNumber("Semester filter (0 means all, 1-8 for one semester): ",
                        0, 8, searchSemester) ||
            !readLine("Course code filter (blank means any): ", code, sizeof(code)) ||
            !readLine("Course name contains (blank means any): ", name, sizeof(name)) ||
            !readNumber("Credits filter (0 any, 1-9 exact): ", 0, 9, searchCredits) ||
            !readNumber("Type filter (0 any, 1 MAIN, 2 ELECTIVE): ", 0, 2, searchType) ||
            !readNumber("Group filter (0 any, 1 CHEMISTRY, 2 PHYSICS, 3 BOTH): ",
                        0, 3, searchGroup)) return 1;
        if (searchType == 1) typeFilter = "MAIN";
        else if (searchType == 2) typeFilter = "ELECTIVE";
        if (searchGroup == 1) groupFilter = "CHEMISTRY";
        else if (searchGroup == 2) groupFilter = "PHYSICS";
        else if (searchGroup == 3) groupFilter = "BOTH";
        std::cout << "\nCatalog search results:\n";
        catalog.search(searchSemester, specialization,
                       code[0] ? code : 0, typeFilter, groupFilter,
                       name[0] ? name : 0, searchCredits);
    }

    bool whatIf;
    if (!askYesNo("Show a read-only what-if ranking for this semester (y/n)? ", whatIf)) return 1;
    if (whatIf) planner.showWhatIf(catalog, specialization, group,
                                   currentSemester, prerequisites);

    if (!completions.save("data/course_completion.csv", catalog)) {
        std::cerr << "Could not save completion changes to data/course_completion.csv.\n";
        return 1;
    }
    std::cout << "Completion data saved in the existing six-field pipe-separated format.\n";
    std::cout << "No verified graduation target is available, so remaining credits are not calculated.\n";
    return 0;
}
