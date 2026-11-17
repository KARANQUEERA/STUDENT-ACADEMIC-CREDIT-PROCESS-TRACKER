import sys
import tempfile
import unittest
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT_ROOT / "web_app"))

try:
    from app import _credit_value_is_valid, create_app
    from db import connect
except ModuleNotFoundError as error:
    if error.name != "flask":
        raise
    create_app = None
    connect = None
    _credit_value_is_valid = None


@unittest.skipIf(create_app is None, "Flask dependency is not installed")
class SubjectPriorityTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir=PROJECT_ROOT / "tests")
        self.database = Path(self.temporary.name) / "priority.sqlite3"
        app = create_app(self.database)
        app.config["TESTING"] = True
        self.client = app.test_client()

    def tearDown(self):
        self.temporary.cleanup()

    def add_course(self, semester, code, name, credits, course_type="MAIN"):
        with connect(self.database) as connection:
            cursor = connection.execute(
                "INSERT INTO courses(semester, code, name, credits, course_type, "
                "specialization, course_group) VALUES (?, ?, ?, ?, ?, 'ALL', 'BOTH')",
                (semester, code, name, credits, course_type),
            )
            return cursor.lastrowid

    def test_returns_only_the_selected_semester_and_keeps_course_types(self):
        target = self.add_course(3, "S3MAIN", "Semester 3 main", 4)
        elective = self.add_course(3, "S3ELEC", "Semester 3 elective", 3, "ELECTIVE")
        other = self.add_course(4, "S4MAIN", "Semester 4 main", 5)

        response = self.client.get("/api/subject-priority?semester=3")

        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.json["semester"], 3)
        self.assertEqual(
            {course["course_id"] for course in response.json["courses"]},
            {target, elective},
        )
        self.assertNotIn(other, {course["course_id"] for course in response.json["courses"]})
        self.assertEqual(
            {course["course_type"] for course in response.json["courses"]},
            {"MAIN", "ELECTIVE"},
        )

    def test_sorts_by_credits_and_equal_credits_share_rank(self):
        four_a = self.add_course(5, "TIE-A", "Four credits A", 4)
        four_b = self.add_course(5, "TIE-B", "Four credits B", 4, "ELECTIVE")
        two = self.add_course(5, "LOWER", "Two credits", 2)

        response = self.client.get("/api/subject-priority?semester=5")

        self.assertEqual(response.status_code, 200)
        rows = response.json["courses"]
        self.assertEqual([row["course_id"] for row in rows], [four_a, four_b, two])
        self.assertEqual([row["rank"] for row in rows], [1, 1, 3])
        self.assertEqual(response.json["total_semester_credits"], 10)
        self.assertEqual([row["credit_weight_percent"] for row in rows], [40.0, 40.0, 20.0])

    def test_empty_semester_returns_empty_result(self):
        response = self.client.get("/api/subject-priority?semester=7")
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.json["courses"], [])
        self.assertEqual(response.json["total_semester_credits"], 0)

    def test_invalid_semester_is_rejected(self):
        for value in ("", "0", "9", "2.5", "x"):
            with self.subTest(value=value):
                response = self.client.get(f"/api/subject-priority?semester={value}")
                self.assertEqual(response.status_code, 400)
                self.assertIn("semester", response.json["error"])

    def test_invalid_stored_credit_values_are_not_ranked_or_counted(self):
        valid = self.add_course(6, "VALID", "Valid credit value", 3)
        invalid_text = self.add_course(6, "BAD-TEXT", "Invalid text credit", 2)
        invalid_negative = self.add_course(6, "BAD-NEG", "Negative credit", 1)
        with connect(self.database) as connection:
            connection.execute("PRAGMA ignore_check_constraints = ON")
            connection.execute("UPDATE courses SET credits = 'unknown' WHERE course_id = ?",
                               (invalid_text,))
            connection.execute("UPDATE courses SET credits = -1 WHERE course_id = ?",
                               (invalid_negative,))

        response = self.client.get("/api/subject-priority?semester=6")

        self.assertEqual(response.status_code, 200)
        rows = {row["course_id"]: row for row in response.json["courses"]}
        self.assertEqual(response.json["total_semester_credits"], 3)
        self.assertEqual(rows[valid]["rank"], 1)
        for course_id in (invalid_text, invalid_negative):
            self.assertIsNone(rows[course_id]["rank"])
            self.assertIsNone(rows[course_id]["credit_weight_percent"])
            self.assertFalse(rows[course_id]["credit_data_valid"])
        self.assertEqual(response.json["invalid_credit_course_count"], 2)

    def test_missing_or_non_integer_credit_values_fail_validation(self):
        for value in (None, "unknown", -1, 2.5):
            with self.subTest(value=value):
                self.assertFalse(_credit_value_is_valid(value))
        self.assertTrue(_credit_value_is_valid(0))
        self.assertTrue(_credit_value_is_valid(3))


if __name__ == "__main__":
    unittest.main()
