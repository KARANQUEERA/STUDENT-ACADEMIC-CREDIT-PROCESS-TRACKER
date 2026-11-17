"""Local Flask API for the Credit Tracker. Catalog loading is a separate step."""

import os
from pathlib import Path

from flask import Flask, jsonify, request, send_from_directory

from db import connect, initialize_database
from prerequisite_validation import evaluate_prerequisites
from recommendations import recommend_electives, recommend_for_semester


VALID_STATUSES = {"COMPLETED", "IN_PROGRESS", "NOT_STARTED", "FAILED"}
ROOT = Path(__file__).resolve().parent
RECOMMENDATION_PREFERENCES = {
	"easy going": "Easy-going",
    "balanced": "Balanced",
    "challenge me": "Challenge me",
}


def _prerequisite_result(connection, course_id):
    """Evaluate only the reviewed rules; legacy edges are audit-only."""
    return evaluate_prerequisites(connection, course_id, student_id="local")


def _credit_value_is_valid(value):
    """Catalog credits are whole, non-negative numbers; reject other stored values."""
    return isinstance(value, int) and not isinstance(value, bool) and value >= 0


def create_app(database_path=None):
    app = Flask(__name__, static_folder=str(ROOT / "static"), static_url_path="/static")
    selected_path = database_path or os.environ.get(
        "CREDIT_TRACK_DB", str(ROOT / "instance" / "credit_tracker.sqlite3")
    )
    initialize_database(selected_path)
    app.config["DATABASE_PATH"] = str(selected_path)

    def open_database():
        return connect(app.config["DATABASE_PATH"])

    @app.get("/")
    def home():
        return send_from_directory(app.static_folder, "index.html")

    @app.get("/api/courses")
    def list_courses():
        conditions = []
        values = []
        for parameter, column in (("semester", "c.semester"), ("specialization", "c.specialization"),
                                  ("group", "c.course_group"), ("type", "c.course_type")):
            value = request.args.get(parameter)
            if value:
                conditions.append(column + " = ?")
                values.append(value)
        search = request.args.get("search", "").strip()
        if search:
            conditions.append("(c.code LIKE ? OR c.name LIKE ?)")
            values.extend(["%" + search + "%", "%" + search + "%"])
        where = " WHERE " + " AND ".join(conditions) if conditions else ""
        query = (
            "SELECT c.course_id, c.semester, c.code, c.name, c.credits, c.course_type, "
            "c.elective_category, "
            "c.specialization, c.course_group, COALESCE(sc.status, 'NOT_STARTED') AS status, "
            "pd.raw_value AS prerequisite_text, "
            "pd.state AS prerequisite_state, prs.rule_status FROM courses c "
            "LEFT JOIN student_courses sc ON sc.course_id = c.course_id AND sc.student_id = ? "
            "LEFT JOIN prerequisite_data pd ON pd.course_id = c.course_id"
            " LEFT JOIN prerequisite_rule_sets prs ON prs.course_id = c.course_id"
            + where + " ORDER BY c.semester, c.code, c.course_id"
        )
        with open_database() as connection:
            rows = connection.execute(query, ["local"] + values).fetchall()
        return jsonify([dict(row) for row in rows])

    @app.get("/api/courses/<int:course_id>/profile")
    def course_profile(course_id):
        query = (
            "SELECT c.course_id, c.semester, c.code AS course_code, c.course_type, "
            "c.specialization, c.course_group, cp.course_name, cp.short_description, "
            "cp.detailed_description, cp.learning_outcomes, cp.important_topics, "
            "cp.technical_skills, cp.practical_applications, cp.primary_domain, "
            "cp.secondary_domains, cp.programming_intensity, "
            "cp.mathematical_intensity, cp.theory_practical_orientation, "
            "cp.expected_background, cp.career_domains, cp.potential_career_roles, "
            "cp.future_learning, cp.source_reference, cp.verification_status "
            "FROM course_profiles cp JOIN courses c ON c.course_id = cp.course_id "
            "WHERE cp.course_id = ?"
        )
        with open_database() as connection:
            row = connection.execute(query, (course_id,)).fetchone()
        if row is None:
            return jsonify({"error": "course profile not found"}), 404
        profile = dict(row)
        profile["group"] = profile.pop("course_group")
        return jsonify(profile)

    @app.put("/api/courses/<int:course_id>/status")
    def update_status(course_id):
        body = request.get_json(silent=True)
        if not isinstance(body, dict) or body.get("status") not in VALID_STATUSES:
            return jsonify({"error": "status must be COMPLETED, IN_PROGRESS, NOT_STARTED, or FAILED"}), 400
        status = body["status"]
        with open_database() as connection:
            course = connection.execute(
                "SELECT course_id FROM courses WHERE course_id = ?", (course_id,)
            ).fetchone()
            if course is None:
                return jsonify({"error": "course not found"}), 404
            connection.execute(
                "INSERT INTO students(student_id) VALUES ('local') "
                "ON CONFLICT(student_id) DO NOTHING"
            )
            connection.execute(
                "INSERT INTO student_courses(student_id, course_id, status) VALUES (?, ?, ?) "
                "ON CONFLICT(student_id, course_id) DO UPDATE SET status = excluded.status",
                ("local", course_id, status),
            )
        return jsonify({"course_id": course_id, "status": status})

    @app.get("/api/dashboard")
    def dashboard():
        query = (
            "SELECT COALESCE(sc.status, 'NOT_STARTED') AS status, c.course_type, "
            "SUM(c.credits) AS credits, COUNT(*) AS courses FROM courses c "
            "LEFT JOIN student_courses sc ON sc.course_id = c.course_id AND sc.student_id = ? "
            "GROUP BY COALESCE(sc.status, 'NOT_STARTED'), c.course_type"
        )
        summary = {"earned_credits": 0, "completed_compulsory_credits": 0,
                   "completed_elective_credits": 0, "courses_by_status": {}}
        with open_database() as connection:
            rows = connection.execute(query, ("local",)).fetchall()
        for row in rows:
            summary["courses_by_status"].setdefault(row["status"], 0)
            summary["courses_by_status"][row["status"]] += row["courses"]
            if row["status"] == "COMPLETED":
                credits = row["credits"] or 0
                summary["earned_credits"] += credits
                if row["course_type"] == "MAIN":
                    summary["completed_compulsory_credits"] += credits
                elif row["course_type"] == "ELECTIVE":
                    summary["completed_elective_credits"] += credits
        summary["graduation_target_credits"] = None
        return jsonify(summary)

    @app.get("/api/subject-priority")
    def subject_priority():
        semester_value = request.args.get("semester", "")
        if not semester_value.isdecimal() or not 1 <= int(semester_value) <= 8:
            return jsonify({"error": "semester must be an integer from 1 to 8"}), 400

        semester = int(semester_value)
        with open_database() as connection:
            rows = connection.execute(
                "SELECT course_id, semester, code, name, credits, course_type, "
                "elective_category FROM courses WHERE semester = ?",
                (semester,),
            ).fetchall()

        valid_rows = []
        invalid_rows = []
        for row in rows:
            course = dict(row)
            credits = course["credits"]
            if _credit_value_is_valid(credits):
                valid_rows.append(course)
            else:
                course["credits"] = None
                course["credit_data_valid"] = False
                course["data_warning"] = "Credit value is missing or invalid; this course is not ranked."
                invalid_rows.append(course)

        valid_rows.sort(key=lambda course: (-course["credits"], course["code"], course["course_id"]))
        total_credits = sum(course["credits"] for course in valid_rows)
        previous_credits = None
        current_rank = None
        for index, course in enumerate(valid_rows):
            if course["credits"] != previous_credits:
                current_rank = index + 1
                previous_credits = course["credits"]
            course["rank"] = current_rank
            course["credit_data_valid"] = True
            course["credit_weight_percent"] = (
                round(course["credits"] / total_credits * 100, 1)
                if total_credits > 0 else None
            )

        for course in invalid_rows:
            course["rank"] = None
            course["credit_weight_percent"] = None

        return jsonify({
            "semester": semester,
            "total_semester_credits": total_credits,
            "invalid_credit_course_count": len(invalid_rows),
            "courses": valid_rows + invalid_rows,
        })

    @app.get("/api/recommendations")
    def elective_recommendations():
        requested_preference = request.args.get("preference", "Balanced")
        preference_key = " ".join(
            requested_preference.strip().casefold().replace("-", " ").split()
        )
        preference = RECOMMENDATION_PREFERENCES.get(preference_key)
        if preference is None:
            return jsonify({
                "error": "preference must be Easy-going, Balanced, or Challenge me"
            }), 400

        with open_database() as connection:
            results = recommend_electives(
                connection, student_id="local", preference=preference
            )
            elective_metadata = {
                row["course_id"]: {
                    "difficulty_level": row["difficulty_level"],
                    "elective_category": row["elective_category"],
                }
                for row in connection.execute(
                    "SELECT course_id, difficulty_level, elective_category FROM courses "
                    "WHERE course_type = 'ELECTIVE'"
                ).fetchall()
            }

        for recommendation in results:
            metadata = elective_metadata.get(recommendation["course_id"], {})
            recommendation["difficulty_level"] = metadata.get("difficulty_level")
            recommendation["elective_category"] = metadata.get("elective_category")
        return jsonify({"preference": preference, "recommendations": results})

    @app.post("/api/recommendations/semester")
    def semester_elective_recommendations():
        body = request.get_json(silent=True)
        if not isinstance(body, dict):
            return jsonify({"error": "request body must be a JSON object"}), 400

        current_semester = body.get("current_semester")
        target_semester = body.get("target_semester")
        for name, value in (("current_semester", current_semester),
                            ("target_semester", target_semester)):
            if type(value) is not int or not 1 <= value <= 8:
                return jsonify({"error": f"{name} must be an integer from 1 to 8"}), 400
        if target_semester < current_semester:
            return jsonify({
                "error": "target_semester cannot be earlier than current_semester"
            }), 400

        specialization = body.get("specialization")
        if not isinstance(specialization, str):
            return jsonify({"error": "specialization must be CORE, CS, DS, or AIML"}), 400
        preference_value = body.get("preference", "Balanced")
        if not isinstance(preference_value, str):
            return jsonify({
                "error": "preference must be Easy-going, Balanced, or Challenge me"
            }), 400
        preference_key = " ".join(
            preference_value.strip().casefold().replace("-", " ").split()
        )
        preference = RECOMMENDATION_PREFERENCES.get(preference_key)
        if preference is None:
            return jsonify({
                "error": "preference must be Easy-going, Balanced, or Challenge me"
            }), 400

        previous_electives = body.get("previous_electives")
        if previous_electives is not None:
            if (not isinstance(previous_electives, list)
                    or any(type(course_id) is not int or course_id < 1
                           for course_id in previous_electives)):
                return jsonify({
                    "error": "previous_electives must be a list of positive catalog course IDs or null"
                }), 400
            if current_semester < 4:
                if previous_electives:
                    return jsonify({
                        "error": "previous electives are not applicable before Semester 4"
                    }), 400
                # An empty list from an older client is equivalent to no question asked.
                previous_electives = None

        requested_scenarios = body.get("scenarios")
        scenarios = None
        if requested_scenarios is not None:
            if not isinstance(requested_scenarios, list) or not requested_scenarios:
                return jsonify({
                    "error": "scenarios must be a non-empty list when supplied"
                }), 400
            scenarios = []
            seen_scenario_ids = set()
            for index, scenario in enumerate(requested_scenarios, start=1):
                if not isinstance(scenario, dict):
                    return jsonify({"error": f"scenario {index} must be a JSON object"}), 400
                scenario_id = str(scenario.get("scenario_id") or f"scenario-{index}").strip()
                label = str(scenario.get("label") or scenario_id).strip()
                course_ids = scenario.get("course_ids", [])
                if not scenario_id or scenario_id in seen_scenario_ids:
                    return jsonify({
                        "error": "scenario IDs must be non-empty and unique"
                    }), 400
                if (not isinstance(course_ids, list)
                        or any(type(course_id) is not int or course_id < 1
                               for course_id in course_ids)):
                    return jsonify({
                        "error": f"scenario '{scenario_id}' course_ids must be positive catalog course IDs"
                    }), 400
                seen_scenario_ids.add(scenario_id)
                scenarios.append({
                    "scenario_id": scenario_id,
                    "label": label,
                    "course_ids": course_ids,
                })

        engine_arguments = {
            "current_semester": current_semester,
            "specialization": specialization,
            "target_semester": target_semester,
            "previous_elective_course_ids": previous_electives,
            "student_id": "local",
            "preference": preference,
        }
        with open_database() as connection:
            try:
                if scenarios is None:
                    result = recommend_for_semester(
                        connection, **engine_arguments
                    )
                else:
                    scenario_results = []
                    response_base = None
                    for scenario in scenarios:
                        try:
                            scenario_result = recommend_for_semester(
                                connection,
                                planned_scenarios=[scenario],
                                **engine_arguments,
                            )
                        except ValueError as error:
                            return jsonify({
                                "error": f"Scenario '{scenario['scenario_id']}' is invalid: {error}"
                            }), 400
                        if response_base is None:
                            response_base = {
                                key: value for key, value in scenario_result.items()
                                if key != "scenarios"
                            }
                        scenario_results.extend(scenario_result["scenarios"])
                    result = response_base
                    result["scenarios"] = scenario_results
            except ValueError as error:
                return jsonify({"error": str(error)}), 400

            elective_metadata = {
                row["course_id"]: {
                    "difficulty_level": row["difficulty_level"],
                    "elective_category": row["elective_category"],
                }
                for row in connection.execute(
                    "SELECT course_id, difficulty_level, elective_category FROM courses "
                    "WHERE course_type = 'ELECTIVE'"
                ).fetchall()
            }

        for scenario in result["scenarios"]:
            for recommendation in scenario["recommendations"]:
                metadata = elective_metadata.get(recommendation["course_id"], {})
                recommendation["difficulty_level"] = metadata.get("difficulty_level")
                recommendation["elective_category"] = metadata.get("elective_category")
        result["preference"] = preference
        return jsonify(result)

    @app.get("/api/courses/<int:course_id>/prerequisites")
    def check_prerequisites(course_id):
        with open_database() as connection:
            result = _prerequisite_result(connection, course_id)
            if result is None:
                return jsonify({"error": "course not found"}), 404
        return jsonify(result)

    @app.post("/api/planner/preview")
    def preview_plan():
        body = request.get_json(silent=True)
        if not isinstance(body, dict) or not isinstance(body.get("course_ids"), list):
            return jsonify({"error": "course_ids must be a list of course IDs"}), 400
        course_ids = body["course_ids"]
        if any(type(course_id) is not int or course_id < 1 for course_id in course_ids):
            return jsonify({"error": "course_ids must contain positive integers"}), 400
        if len(set(course_ids)) != len(course_ids):
            return jsonify({"error": "course_ids must not contain duplicates"}), 400

        courses = []
        totals = {"total_credits": 0, "compulsory_credits": 0,
                  "elective_option_credits": 0, "other_credits": 0}
        elective_semesters = {}
        with open_database() as connection:
            for course_id in course_ids:
                course = connection.execute(
                    "SELECT course_id, semester, code, name, credits, course_type, "
                    "elective_category "
                    "FROM courses WHERE course_id = ?", (course_id,)
                ).fetchone()
                if course is None:
                    return jsonify({"error": f"course {course_id} was not found"}), 404
                if course["course_type"] == "ELECTIVE":
                    semester = course["semester"]
                    if semester == 8:
                        category = course["elective_category"]
                        if category not in {"PEC", "OEC"}:
                            return jsonify({
                                "error": (
                                    f"Semester 8 elective {course['code']} has no PEC/OEC "
                                    "category and cannot be added to a plan yet."
                                )
                            }), 400
                        occupied_categories = elective_semesters.setdefault(semester, set())
                        if category in occupied_categories:
                            return jsonify({
                                "error": (
                                    f"Only one {category} elective may be selected "
                                    "for Semester 8."
                                )
                            }), 400
                        occupied_categories.add(category)
                    else:
                        occupied_categories = elective_semesters.setdefault(semester, set())
                        if occupied_categories:
                            return jsonify({
                                "error": (
                                    "Only one elective may be selected per semester "
                                    f"(semester {semester})."
                                )
                            }), 400
                        occupied_categories.add("ELECTIVE")
                course_data = dict(course)
                totals["total_credits"] += course["credits"]
                if course["course_type"] == "MAIN":
                    totals["compulsory_credits"] += course["credits"]
                elif course["course_type"] == "ELECTIVE":
                    totals["elective_option_credits"] += course["credits"]
                else:
                    totals["other_credits"] += course["credits"]
                courses.append(course_data)
        return jsonify({"courses": courses, "totals": totals,
                        "saved_statuses_changed": False,
                        "notice": "This is a hypothetical plan, not an approved registration."})

    return app


if __name__ == "__main__":
    create_app().run(host="127.0.0.1", port=5000, debug=False)
