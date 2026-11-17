# Student Academic Credit Process Tracker

A student project for reviewing course offerings, tracking completion and credits, exploring electives, and learning data structures and object-oriented programming. It has three deliberately separate parts: a C console program, a C++ console program, and a Flask/SQLite web application.

## Current project data

- `data/course_catalog_with_difficulty.csv` is the current 156-course catalog: 118 `MAIN` courses and 38 `ELECTIVE` records. Its 18 fields include prerequisite source text, difficulty/research metadata, and explicit Semester 8 `PEC`/`OEC` categories. The catalog includes two placeholder elective slots.
- `data/course_profiles_master_38_v3.csv` contains 38 supplemental course profiles. These are linked to catalog rows by their full course identity, not by course code alone. The profile CSV includes verification labels; a label is not a substitute for an authoritative curriculum source.
- The local SQLite database currently contains the 156 catalog rows and 38 profiles. `web_app/instance/` is intentionally ignored by Git and is not distributed. A fresh checkout therefore needs an explicit catalog/profile import before the dashboard will show courses.
- `data/course_completion.csv` is local student progress data. It is intentionally ignored by Git and is not intended for public publication. Do not use it as a disposable test file.

The active data files are `data/course_catalog_with_difficulty.csv` and `data/course_profiles_master_38_v3.csv`. These are the two data files intended for public publication; the completion file is local progress data and should remain private.

## Folder structure

```text
src_c/                     C console application and linked-list module
src_cpp/                   C++ console application and OOP/DSA classes
web_app/                   Flask app, SQLite schema/helpers, importers, recommendation logic
web_app/static/            HTML, CSS, and browser JavaScript
web_app/instance/          Local SQLite database and backups (ignored by Git)
data/                      Active catalog, V3 course profiles, and local progress data
tests/                     Python unittest suite using temporary databases/fixtures
backups/                   Local project/data backups (ignored by Git)
phase5_msvc_verification/  Native compiler verification harness and generated artifacts
```

## C console component

`src_c/main.c`, `src_c/course_list.c`, `src_c/course_list.h`, `src_c/course_search.c`, and `src_c/course_credit_report.c` implement a C console program. `Course` records are stored in a manually allocated singly linked list. The CSV reader handles the current 18-column header, quoted fields, escaped quotes, and quoted multi-line fields; the C module retains the course fields needed for its display and profile filtering. It filters by semester and specialization, with Chemistry/Physics group selection for Semesters 1–2, lists main courses separately from elective options, and asks for completion status for the selected profile. The additional menu supports status-aware course search and a completed-course ledger with compulsory and elective-option credits reported separately.

Completion is stored in the pipe-separated `data/course_completion.csv` file. The file contains local progress and should be backed up before direct use. For a demonstration, use a temporary working directory with copies of the catalog and completion file; the program rewrites the completion file in its working directory when it saves.

## C++ console component

`src_cpp/main.cpp`, `src_cpp/course.cpp`, `src_cpp/course.h`, `src_cpp/course_search.cpp`, and `src_cpp/course_credit_report.cpp` implement a separate C++ console program. `Course` encapsulates its data with private members and public constructors/getters. `CourseCatalog` and `CompletionStore` use manually implemented linked lists. The program loads the catalog, reviews saved semester history, supports status-aware catalog search, and can print a completed-course ledger with compulsory and elective-option credits reported separately.

`CoursePlanner` uses a manually implemented Merge Sort to rank a temporary what-if view. `PrerequisiteGraph` represents manually added prerequisite edges as adjacency lists and uses recursive depth-first traversal with cycle detection. The current `main.cpp` does not populate that graph from the catalog or database, so the native program reports prerequisite data as unavailable. The native planner is a ranked preview; it does not apply the web planner's PEC/OEC or per-semester elective limits. No STL containers or templates are used.

Both native programs are standalone console executables. **They are not integrated with Flask, the SQLite database, or each other's course classes.** Both read `data/course_catalog_with_difficulty.csv`; both use the pipe-separated completion file in their current working directory. The C++ program treats a missing completion file as a first-run case and starts with all courses incomplete. If an existing completion file cannot be read or validated, it refuses to save over it.

## Flask application and SQLite

`web_app/app.py` serves the static dashboard and JSON API. `web_app/db.py` provides SQLite connections and additive schema initialization from `web_app/schema.sql`. On startup, Flask initializes the schema but does not import a catalog or profiles. The default database is `web_app/instance/credit_tracker.sqlite3`; the `CREDIT_TRACK_DB` environment variable can select another path.

The dashboard supports course search/filtering, saved status updates, credit summaries, prerequisite information, a hypothetical course planner, elective recommendations, and supplemental course-profile dialogs. The web application stores status in SQLite under a single local student ID (`local`); it does not provide user accounts or multi-student authentication.

### Elective recommendations

The engine in `web_app/recommendations.py` uses `web_app/course_knowledge_map.csv`. Mappings are keyed by full course identity, and only reviewed high-confidence mappings contribute to numerical background matches. The baseline personalized score is:

```text
D = max(1, B - min(1, V/N))
```

Here `B` is the catalog base difficulty, `V` is the number of distinct verified background areas covered, and `N` is the number of normalized required areas. Scores are rounded to one decimal place. Missing base difficulty leaves a course unscored; placeholders are not scored; low-confidence ratings carry warnings. Recommendations are not blocked by prerequisites.

The ranking preferences are:

- **Easy-going:** lower personalized difficulty first.
- **Balanced:** closest to difficulty 3 first.
- **Challenge me:** higher personalized difficulty first.

The guided, semester-aware recommender accepts a current semester, specialization, target semester, previous-elective information, preference, and optional independent hypothetical scenarios. Actual completed courses are distinguished from assumptions. For planning, earlier main courses may be projected as completed, and scenario electives may be added hypothetically; these assumptions do not update saved statuses. Completed target electives are excluded, while placeholder slots remain visible without a score.

### What-if planner and elective limits

`POST /api/planner/preview` validates selected catalog IDs and returns a hypothetical credit breakdown without changing saved statuses. Main courses are unrestricted. Semesters 1–7 allow at most one elective per semester. Semester 8 allows at most one PEC and one OEC. The preview does not establish registration approval and does not perform prerequisite eligibility validation.

### Prerequisite limitations

The prerequisite API uses the reviewed rule-set/group/option/evidence tables and ignores legacy edges for eligibility. Catalog text marked `Nil` means only that no prerequisite is listed; it is not official verification. **The current database has no confirmed prerequisite rules populated.** Consequently, the app must report listed, pending, or unresolved states rather than claiming definitive eligibility. Do not infer prerequisites from punctuation or course-code similarity.

## Install and run the web app

Use Python 3.10 or later. Flask is the only third-party runtime dependency; SQLite comes from Python's standard library.

From PowerShell at the project root:

```powershell
py -3 -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
```

The database is intentionally not published. On a fresh checkout, import the finalized catalog and the supplemental profiles into the local database. These commands write to SQLite. If using a database that already contains courses or progress, back it up and review the importer behavior first. The commands below omit `--completion-file`; they do not import the legacy completion CSV.

```powershell
python -B web_app\catalog_importer.py --database web_app\instance\credit_tracker.sqlite3 --catalog data\course_catalog_with_difficulty.csv --catalog-is-final
python -B web_app\course_profile_importer.py --database web_app\instance\credit_tracker.sqlite3 --profiles data\course_profiles_master_38_v3.csv
python web_app\app.py
```

Open <http://127.0.0.1:5000/>. On startup, `create_app()` initializes missing schema objects but does not import CSV data. If the local database is empty, the catalog importer must be run before the dashboard will list courses. Stop the server with `Ctrl+C` in its terminal.

## Build and run native programs safely on Windows

The verified native builds used Microsoft Visual Studio Build Tools from an **x64 Native Tools Command Prompt for VS**. The current MinGW `gcc`/`g++` installation fails to start with “Illegal System DLL Relocation” on the development machine. GCC builds have not been verified. MSVC reports C4996 secure-CRT warnings for some standard C library calls; these were not hidden or fixed during verification.

To keep executables, object files, and writable progress data outside the project, first prepare isolated run folders from PowerShell. This uses the actual first catalog identity to create a temporary incomplete-status row; it does not copy or alter the real completion file:

```powershell
$project = (Get-Location).Path
$first = Import-Csv -LiteralPath (Join-Path $project 'data\course_catalog_with_difficulty.csv') | Select-Object -First 1
foreach ($name in 'c', 'cpp') {
    $run = Join-Path $env:TEMP "credit-track-$name-demo"
    New-Item -ItemType Directory -Force -Path (Join-Path $run 'data') | Out-Null
    Copy-Item -LiteralPath (Join-Path $project 'data\course_catalog_with_difficulty.csv') -Destination (Join-Path $run 'data\course_catalog_with_difficulty.csv')
    $line = '{0}|{1}|{2}|{3}|{4}|0' -f $first.semester, $first.code, $first.type, $first.specialization, $first.group
    Set-Content -LiteralPath (Join-Path $run 'data\course_completion.csv') -Value $line -Encoding ascii
}
```

Then, open an x64 Native Tools Command Prompt at the project root, compile and run each program separately. The working directory is the temporary folder so relative `data\...` paths resolve to the copies above.

```bat
set "PROJECT=%CD%"
pushd "%TEMP%\credit-track-c-demo"
cl /nologo /W4 /TC "%PROJECT%\src_c\main.c" "%PROJECT%\src_c\course_list.c" "%PROJECT%\src_c\course_search.c" "%PROJECT%\src_c\course_credit_report.c" /Fe:tracker_c.exe
tracker_c.exe
popd
```

```bat
set "PROJECT=%CD%"
pushd "%TEMP%\credit-track-cpp-demo"
cl /nologo /W4 /EHsc /TP "%PROJECT%\src_cpp\main.cpp" "%PROJECT%\src_cpp\course.cpp" "%PROJECT%\src_cpp\course_search.cpp" "%PROJECT%\src_cpp\course_credit_report.cpp" /Fe:course_history.exe
course_history.exe
popd
```

`/TC` selects C mode; `/TP` selects C++ mode; `/W4` enables useful MSVC warnings; `/EHsc` enables standard C++ exception handling. MSVC does not provide the exact GCC `-std=c++11` switch; the verified MSVC build compiles this C++ source in MSVC's supported language mode. The source uses basic C++ features and no STL containers or templates.

The native programs save to the completion file in their current working directory. The commands above keep that file temporary. Do not run them from the project root if you need to preserve `data/course_completion.csv` unchanged.

## Tests

From the project root:

```powershell
python -B -m unittest discover -s tests -v
node --check web_app\static\app.js
```

The Python tests use temporary SQLite databases and fixtures; they do not require importing data into the real database. The current suite covers API validation, credit/status persistence, recommendation scoring/ranking/scenarios, prerequisite outcomes, catalog migrations, PEC/OEC planner rules, and profile importing/API behavior. Native malformed-input and sort-order regression checks are not part of this Python suite.

## GitHub and local data safety

`.gitignore` excludes the local SQLite instance, progress CSV, binaries, object files, backup folders, and local environment files. It does not ignore the entire `data/` directory: review CSVs individually before staging. The local database and `data/course_completion.csv` are not included in a normal GitHub checkout; follow the setup/import instructions above to create a local database. Do not commit student progress, private environment files, database backups, or generated verification data.
