"""Local Flask API for the Credit Tracker. Catalog loading is a separate step."""

import os
from pathlib import Path

from flask import Flask, jsonify, request, send_from_directory

from db import connect, initialize_database


VALID_STATUSES = {"COMPLETED", "IN_PROGRESS", "NOT_STARTED", "FAILED"}
ROOT = Path(__file__).resolve().parent


def _prerequisite_result(connection, course_id):
    """Check saved prerequisite edges and their transitive requirements."""
    target = connection.execute(
        "SELECT course_id, code FROM courses WHERE course_id = ?", (course_id,)
    ).fetchone()
    if target is None:
        return None

    direct_rows = connection.execute(
        "SELECT required.course_id, required.code, "
        "COALESCE(status.status, 'NOT_STARTED') AS status "
        "FROM prerequisites edge "
        "JOIN courses required ON required.course_id = edge.required_course_id "
        "LEFT JOIN student_courses status ON status.course_id = required.course_id "
        "AND status.student_id = ? WHERE edge.course_id = ? ORDER BY required.code",
        ("local", course_id),
    ).fetchall()
    target_data = connection.execute(
        "SELECT state FROM prerequisite_data WHERE course_id = ?", (course_id,)
    ).fetchone()
    if target_data is None or target_data["state"] == "UNRESOLVED":
        return {"state": "UNRESOLVED", "eligible": False,
                "required_courses": [dict(row) for row in direct_rows],
                "incomplete_courses": [], "unresolved_courses": [target["code"]],
                "cycle": []}
    if target_data["state"] == "NO_PREREQUISITE":
        if direct_rows:
            return {"state": "UNRESOLVED", "eligible": False,
                    "required_courses": [dict(row) for row in direct_rows],
                    "incomplete_courses": [], "unresolved_courses": [target["code"]],
                    "cycle": []}
        return {"state": "NO_PREREQUISITE", "eligible": True,
                "required_courses": [], "incomplete_courses": [],
                "unresolved_courses": [], "cycle": []}

    colors = {}
    stack = []
    incomplete = {}
    unresolved = set()
    cycle = []

    def visit(current_id):
        color = colors.get(current_id, 0)
        if color == 1:
            cycle_start = stack.index(current_id)
            cycle.extend(stack[cycle_start:] + [current_id])
            return
        if color == 2:
            return

        colors[current_id] = 1
        stack.append(current_id)
        data = connection.execute(
            "SELECT c.code, pd.state FROM courses c "
            "LEFT JOIN prerequisite_data pd ON pd.course_id = c.course_id "
            "WHERE c.course_id = ?", (current_id,)
        ).fetchone()
        if data is None or data["state"] in (None, "UNRESOLVED"):
            if data is not None:
                unresolved.add(data["code"])
        elif data["state"] == "NO_PREREQUISITE":
            has_edges = connection.execute(
                "SELECT 1 FROM prerequisites WHERE course_id = ? LIMIT 1", (current_id,)
            ).fetchone()
            if has_edges:
                unresolved.add(data["code"])
        elif data["state"] == "VERIFIED":
            requirements = connection.execute(
                "SELECT required.course_id, required.code, "
                "COALESCE(status.status, 'NOT_STARTED') AS status "
                "FROM prerequisites edge "
                "JOIN courses required ON required.course_id = edge.required_course_id "
                "LEFT JOIN student_courses status ON status.course_id = required.course_id "
                "AND status.student_id = ? WHERE edge.course_id = ? ORDER BY required.code",
                ("local", current_id),
            ).fetchall()
            if not requirements:
                unresolved.add(data["code"])
            for requirement in requirements:
                if requirement["status"] != "COMPLETED":
                    incomplete[requirement["course_id"]] = {
                        "course_id": requirement["course_id"],
                        "code": requirement["code"],
                        "status": requirement["status"],
                    }
                visit(requirement["course_id"])
        else:
            unresolved.add(data["code"])
        stack.pop()
        colors[current_id] = 2

    visit(course_id)
    cycle_codes = []
    if cycle:
        for cycle_course_id in cycle:
            cycle_course = connection.execute(
                "SELECT code FROM courses WHERE course_id = ?", (cycle_course_id,)
            ).fetchone()
            if cycle_course is not None:
                cycle_codes.append(cycle_course["code"])

    if cycle_codes:
        state = "CYCLE_DETECTED"
    elif unresolved:
        state = "UNRESOLVED"
    elif incomplete:
        state = "INCOMPLETE"
    else:
        state = "SATISFIED"
    return {
        "state": state,
        "eligible": state == "SATISFIED",
        "required_courses": [dict(row) for row in direct_rows],
        "incomplete_courses": list(incomplete.values()),
        "unresolved_courses": sorted(unresolved),
        "cycle": cycle_codes,
    }


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
            "c.specialization, c.course_group, COALESCE(sc.status, 'NOT_STARTED') AS status, "
            "pd.raw_value AS prerequisite_text, "
            "pd.state AS prerequisite_state FROM courses c "
            "LEFT JOIN student_courses sc ON sc.course_id = c.course_id AND sc.student_id = ? "
            "LEFT JOIN prerequisite_data pd ON pd.course_id = c.course_id"
            + where + " ORDER BY c.semester, c.code, c.course_id"
        )
        with open_database() as connection:
            rows = connection.execute(query, ["local"] + values).fetchall()
        return jsonify([dict(row) for row in rows])

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
        with open_database() as connection:
            for course_id in course_ids:
                course = connection.execute(
                    "SELECT course_id, semester, code, name, credits, course_type "
                    "FROM courses WHERE course_id = ?", (course_id,)
                ).fetchone()
                if course is None:
                    return jsonify({"error": f"course {course_id} was not found"}), 404
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
