#include "course.h"

#include <cstring>
#include <iostream>

/* Print every completed course, then keep compulsory and elective totals apart. */
void printCompletedCreditReport(const CourseCatalog &catalog,
                                const char *specialization,
                                const char *group)
{
    int lastSemester = 0;
    int totalMainCredits = 0;
    int totalElectiveCredits = 0;
    bool anyCompleted = false;

    for (int index = 0; index < catalog.size(); ++index) {
        const Course *course = catalog.at(index);
        if (course->getSemester() > lastSemester) lastSemester = course->getSemester();
    }

    std::cout << "\nCOMPLETED COURSE CREDIT REPORT\n";
    std::cout << "Elective options are reported separately from compulsory courses.\n";
    for (int semester = 1; semester <= lastSemester; ++semester) {
        int semesterMainCredits = 0;
        int semesterElectiveCredits = 0;

        for (int index = 0; index < catalog.size(); ++index) {
            const Course *course = catalog.at(index);
            if (course->getSemester() != semester || !course->isCompleted() ||
                !catalog.matchesProfile(*course, semester, specialization, group))
                continue;

            anyCompleted = true;
            std::cout << "Semester " << semester << " | " << course->getCode()
                      << " | " << course->getName() << " | " << course->getType()
                      << " | specialization " << course->getSpecialization()
                      << " | group " << course->getGroup() << " | "
                      << course->getCredits() << " credits\n";
            if (std::strcmp(course->getType(), "MAIN") == 0) {
                semesterMainCredits += course->getCredits();
                totalMainCredits += course->getCredits();
            } else if (std::strcmp(course->getType(), "ELECTIVE") == 0) {
                semesterElectiveCredits += course->getCredits();
                totalElectiveCredits += course->getCredits();
            }
        }
        std::cout << "Semester " << semester << " totals: " << semesterMainCredits
                  << " compulsory credits, " << semesterElectiveCredits
                  << " elective-option credits.\n";
    }

    if (!anyCompleted) std::cout << "No completed courses match this profile.\n";
    std::cout << "Completed compulsory credits: " << totalMainCredits << "\n";
    std::cout << "Completed elective-option credits: " << totalElectiveCredits << "\n";
}