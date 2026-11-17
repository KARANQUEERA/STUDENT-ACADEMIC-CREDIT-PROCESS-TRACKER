const statusOptions = ["NOT_STARTED", "IN_PROGRESS", "COMPLETED", "FAILED"];
const selectedPlanCourseIds = new Set();
const selectedPlanCourseDetails = new Map();
let courseProfileReturnFocus = null;
let recommendationRequestNumber = 0;
let recommendationElectives = [];
let recommendationCatalogError = "";
let recommendationCatalogLoaded = false;
let activeRecommendationStep = "current";
let lastGeneratedRecommendationRequest = null;
let recommendationState = {
  currentSemester: "",
  specialization: "",
  previousState: "unknown",
  previousElectiveId: "",
  targetSemester: "",
  preference: "Balanced",
  scenarios: []
};

function setMessage(message, isError = false) {
  const messageElement = document.querySelector("#message");
  messageElement.textContent = message;
  messageElement.classList.toggle("error", isError);
}

async function loadDashboard() {
  const summary = document.querySelector("#summary");
  try {
    const response = await fetch("/api/dashboard");
    if (!response.ok) throw new Error("The credit summary could not be loaded.");
    const data = await response.json();
    const statusCounts = data.courses_by_status || {};
    const cards = [
      {label: "Earned credits", value: data.earned_credits, unit: "credits", highlight: true},
      {label: "Compulsory credits", value: data.completed_compulsory_credits, unit: "credits"},
      {label: "Elective credits", value: data.completed_elective_credits, unit: "credits"},
      {label: "In progress", value: statusCounts.IN_PROGRESS || 0, unit: "courses"},
      {label: "Incomplete", value: (statusCounts.NOT_STARTED || 0) + (statusCounts.FAILED || 0), unit: "courses"},
      {label: "Graduation target", value: data.graduation_target_credits ?? "Not verified", unit: ""}
    ];

    summary.replaceChildren();
    cards.forEach(card => {
      const metric = document.createElement("article");
      metric.className = `metric${card.highlight ? " metric-highlight" : ""}`;
      const label = document.createElement("span");
      label.className = "metric-label";
      label.textContent = card.label;
      const value = document.createElement("strong");
      value.textContent = card.value ?? 0;
      if (card.unit) {
        const unit = document.createElement("span");
        unit.className = "metric-unit";
        unit.textContent = card.unit;
        value.append(unit);
      }
      metric.append(label, value);
      summary.append(metric);
    });
  } catch (error) {
    summary.innerHTML = "<p class=\"summary-loading\">Credit summary is unavailable. Check that the local application is running.</p>";
    setMessage(error.message, true);
  }
}

function setRecommendationMessage(message, isError = false) {
  const element = document.querySelector("#recommendation-message");
  element.textContent = message;
  element.classList.toggle("error", isError);
}

function recommendationElement(tag, className = "", text = "") {
  const element = document.createElement(tag);
  if (className) element.className = className;
  if (text) element.textContent = text;
  return element;
}

function appliesToSpecialization(course, specialization) {
  const tokens = (course.specialization || "").split("|").map(token => token.trim());
  return tokens.includes("ALL") || tokens.includes(specialization);
}

function electivesForSemester(semester, specialization) {
  return recommendationElectives
    .filter(course => Number(course.semester) === Number(semester) &&
      appliesToSpecialization(course, specialization))
    .sort((left, right) => left.code.localeCompare(right.code) ||
      left.specialization.localeCompare(right.specialization) ||
      left.course_group.localeCompare(right.course_group));
}

function fullElectiveLabel(course) {
  return `Sem ${course.semester} · ${course.code} — ${course.name} ` +
    `[${course.specialization}; ${course.course_group}]`;
}

function availableRecommendationSteps() {
  const steps = ["current", "specialization"];
  if (Number(recommendationState.currentSemester) >= 4) steps.push("previous");
  steps.push("target");
  if (Number(recommendationState.targetSemester) > Number(recommendationState.currentSemester)) {
    steps.push("plans");
  }
  steps.push("preference");
  return steps;
}

const recommendationStepLabels = {
  current: "Current semester",
  specialization: "Specialization",
  previous: "Previous elective",
  target: "Target semester",
  plans: "Elective plans",
  preference: "Preference & review"
};

function renderRecommendationProgress() {
  const progress = document.querySelector("#recommendation-progress");
  progress.replaceChildren();
  const steps = availableRecommendationSteps();
  const currentIndex = steps.indexOf(activeRecommendationStep);
  steps.forEach((step, index) => {
    const item = recommendationElement("li", "recommendation-step-marker");
    if (step === activeRecommendationStep) item.classList.add("active");
    if (index < currentIndex) item.classList.add("complete");
    const number = recommendationElement("span", "step-number", String(index + 1));
    const label = recommendationElement("span", "step-name", recommendationStepLabels[step]);
    item.append(number, label);
    progress.append(item);
  });
}

function hideOldRecommendationResults() {
  recommendationRequestNumber++;
  document.querySelector("#recommendation-results").hidden = true;
}

function updateDependentRecommendationState() {
  const current = Number(recommendationState.currentSemester);
  const specialization = recommendationState.specialization;
  if (!current || !specialization) return;

  if (recommendationState.previousElectiveId) {
    const previous = recommendationElectives.find(course =>
      String(course.course_id) === String(recommendationState.previousElectiveId));
    if (!previous || Number(previous.semester) !== current - 1 ||
        !appliesToSpecialization(previous, specialization)) {
      recommendationState.previousElectiveId = "";
      recommendationState.previousState = "unknown";
    }
  }

  const target = Number(recommendationState.targetSemester);
  if (target && !electivesForSemester(target, specialization)
    .some(course => course.status !== "COMPLETED")) {
    recommendationState.targetSemester = "";
  }

  recommendationState.scenarios.forEach(scenario => {
    Object.keys(scenario.choices).forEach(semester => {
      const selected = recommendationElectives.find(course =>
        String(course.course_id) === String(scenario.choices[semester]));
      if (!selected || !appliesToSpecialization(selected, specialization) ||
          Number(selected.semester) !== Number(semester) ||
          Number(semester) < current || Number(semester) >= Number(recommendationState.targetSemester)) {
        delete scenario.choices[semester];
      }
    });
  });
}

function renderRecommendationStep() {
  const panel = document.querySelector("#recommendation-step");
  const backButton = document.querySelector("#recommendation-back");
  const continueButton = document.querySelector("#recommendation-continue");
  const generateButton = document.querySelector("#recommendation-generate");
  panel.replaceChildren();
  renderRecommendationProgress();

  const steps = availableRecommendationSteps();
  let index = steps.indexOf(activeRecommendationStep);
  if (index < 0) {
    activeRecommendationStep = steps[steps.length - 1];
    index = steps.length - 1;
  }
  backButton.hidden = index === 0;
  continueButton.hidden = index === steps.length - 1;
  generateButton.hidden = index !== steps.length - 1;
  continueButton.disabled = !recommendationCatalogLoaded || Boolean(recommendationCatalogError);
  generateButton.disabled = !recommendationCatalogLoaded || Boolean(recommendationCatalogError);

  if (activeRecommendationStep === "current") {
    panel.append(recommendationElement("h3", "step-question",
      "Which semester are you currently studying in?"));
    const label = recommendationElement("label", "wizard-field");
    label.append(recommendationElement("span", "field-label", "Current semester"));
    const select = recommendationElement("select", "wizard-select");
    select.id = "guided-current-semester";
    select.append(new Option("Choose a semester", ""));
    for (let semester = 1; semester <= 8; semester++) {
      select.append(new Option(`Semester ${semester}`, String(semester)));
    }
    select.value = recommendationState.currentSemester;
    select.addEventListener("change", () => {
      recommendationState.currentSemester = select.value;
      updateDependentRecommendationState();
      hideOldRecommendationResults();
      renderRecommendationProgress();
    });
    label.append(select);
    panel.append(label);
  } else if (activeRecommendationStep === "specialization") {
    panel.append(recommendationElement("h3", "step-question",
      "Which specialization are you studying?"));
    panel.append(recommendationElement("p", "step-help",
      "Combined catalog values apply to each exact token, and courses marked ALL apply to every specialization."));
    const label = recommendationElement("label", "wizard-field");
    label.append(recommendationElement("span", "field-label", "Specialization"));
    const select = recommendationElement("select", "wizard-select");
    select.id = "guided-specialization";
    select.append(new Option("Choose a specialization", ""));
    ["CORE", "CS", "DS", "AIML"].forEach(value => select.append(new Option(value, value)));
    select.value = recommendationState.specialization;
    select.addEventListener("change", () => {
      recommendationState.specialization = select.value;
      updateDependentRecommendationState();
      hideOldRecommendationResults();
      renderRecommendationProgress();
    });
    label.append(select);
    panel.append(label);
  } else if (activeRecommendationStep === "previous") {
    const previousSemester = Number(recommendationState.currentSemester) - 1;
    panel.append(recommendationElement("h3", "step-question",
      "Which elective(s) did you take in your previous semester?"));
    panel.append(recommendationElement("p", "step-help",
      `Previous semester: Semester ${previousSemester}. Your answer is used only as recommendation background.`));
    const choices = [
      ["unknown", "I do not know or prefer not to answer"],
      ["none", "I did not take an elective"],
      ["selected", "I took an elective"]
    ];
    const group = recommendationElement("div", "choice-group");
    choices.forEach(([value, text]) => {
      const label = recommendationElement("label", "choice-option");
      const radio = recommendationElement("input");
      radio.type = "radio";
      radio.name = "previous-elective-state";
      radio.value = value;
      radio.checked = recommendationState.previousState === value ||
        (value === "none" && recommendationState.previousState === "none_reported") ||
        (value === "selected" && recommendationState.previousState === "reported");
      radio.addEventListener("change", () => {
        recommendationState.previousState = value;
        if (value !== "selected") recommendationState.previousElectiveId = "";
        hideOldRecommendationResults();
        renderRecommendationStep();
      });
      label.append(radio, recommendationElement("span", "", text));
      group.append(label);
    });
    panel.append(group);
    if (recommendationState.previousState === "selected" ||
        recommendationState.previousState === "reported") {
      const options = electivesForSemester(previousSemester, recommendationState.specialization);
      if (options.length) {
        const label = recommendationElement("label", "wizard-field");
        label.append(recommendationElement("span", "field-label", "Previous-semester elective"));
        const select = recommendationElement("select", "wizard-select");
        select.id = "guided-previous-elective";
        select.append(new Option("Choose the course", ""));
        options.forEach(course => select.append(new Option(
          fullElectiveLabel(course), String(course.course_id)
        )));
        select.value = recommendationState.previousElectiveId;
        select.addEventListener("change", () => {
          recommendationState.previousElectiveId = select.value;
          hideOldRecommendationResults();
        });
        label.append(select);
        panel.append(label);
      } else {
        panel.append(recommendationElement("p", "recommendation-empty",
          "No previous-semester electives are listed for this specialization."));
      }
    }
  } else if (activeRecommendationStep === "target") {
    panel.append(recommendationElement("h3", "step-question",
      "Which semester's electives would you like recommendations for?"));
    panel.append(recommendationElement("p", "step-help",
      "Choose the current semester or a future semester. Semesters 1 and 2 do not have elective offerings."));
    const availableSemesters = [...new Set(recommendationElectives
      .filter(course => course.status !== "COMPLETED" &&
        appliesToSpecialization(course, recommendationState.specialization))
      .map(course => Number(course.semester))
      .filter(semester => semester >= Number(recommendationState.currentSemester)))]
      .sort((left, right) => left - right);
    if (availableSemesters.length) {
      const label = recommendationElement("label", "wizard-field");
      label.append(recommendationElement("span", "field-label", "Target semester"));
      const select = recommendationElement("select", "wizard-select");
      select.id = "guided-target-semester";
      select.append(new Option("Choose a target semester", ""));
      availableSemesters.forEach(semester => select.append(new Option(
        `Semester ${semester} · ${electivesForSemester(semester, recommendationState.specialization)
          .filter(course => course.status !== "COMPLETED").length} available electives`,
        String(semester)
      )));
      select.value = recommendationState.targetSemester;
      select.addEventListener("change", () => {
        recommendationState.targetSemester = select.value;
        updateDependentRecommendationState();
        hideOldRecommendationResults();
        renderRecommendationProgress();
      });
      label.append(select);
      panel.append(label);
    } else {
      panel.append(recommendationElement("p", "recommendation-empty",
        "There are no applicable elective offerings from your current semester onward."));
    }
  } else if (activeRecommendationStep === "plans") {
    panel.append(recommendationElement("h3", "step-question",
      "Which intervening electives would you like to compare?"));
    panel.append(recommendationElement("p", "step-help",
      "These choices are hypothetical only. Leave any semester undecided if you have not chosen an elective."));
    const target = Number(recommendationState.targetSemester);
    const planSemesters = [];
    for (let semester = Number(recommendationState.currentSemester) + 1; semester < target; semester++) {
      if (electivesForSemester(semester, recommendationState.specialization).length) {
        planSemesters.push(semester);
      }
    }
    if (!planSemesters.length) {
      panel.append(recommendationElement("p", "recommendation-empty",
        "There are no applicable intervening elective choices to add. You can continue without a plan."));
    }
    if (!recommendationState.scenarios.length) {
      recommendationState.scenarios = [{scenario_id: "scenario-1", label: "Scenario A", choices: {}}];
    }
    const scenarios = recommendationElement("div", "scenario-inputs");
    recommendationState.scenarios.forEach((scenario, scenarioIndex) => {
      const card = recommendationElement("section", "scenario-input-card");
      const heading = recommendationElement("div", "scenario-input-heading");
      heading.append(recommendationElement("h4", "", `Scenario ${String.fromCharCode(65 + scenarioIndex)}`));
      if (recommendationState.scenarios.length > 1) {
        const remove = recommendationElement("button", "text-button", "Remove");
        remove.type = "button";
        remove.addEventListener("click", () => {
          recommendationState.scenarios.splice(scenarioIndex, 1);
          hideOldRecommendationResults();
          renderRecommendationStep();
        });
        heading.append(remove);
      }
      card.append(heading);

      const label = recommendationElement("label", "wizard-field");
      label.append(recommendationElement("span", "field-label", "Scenario label"));
      const textInput = recommendationElement("input", "wizard-input");
      textInput.type = "text";
      textInput.maxLength = 60;
      textInput.value = scenario.label;
      textInput.addEventListener("input", () => {
        scenario.label = textInput.value;
        hideOldRecommendationResults();
      });
      label.append(textInput);
      card.append(label);

      planSemesters.forEach(semester => {
        const selectLabel = recommendationElement("label", "wizard-field");
        selectLabel.append(recommendationElement("span", "field-label", `Semester ${semester} elective`));
        const select = recommendationElement("select", "wizard-select");
        select.append(new Option("Undecided", ""));
        electivesForSemester(semester, recommendationState.specialization).forEach(course => {
          select.append(new Option(fullElectiveLabel(course), String(course.course_id)));
        });
        select.value = scenario.choices[semester] || "";
        select.addEventListener("change", () => {
          if (select.value) scenario.choices[semester] = select.value;
          else delete scenario.choices[semester];
          hideOldRecommendationResults();
        });
        selectLabel.append(select);
        card.append(selectLabel);
      });
      scenarios.append(card);
    });
    panel.append(scenarios);
    const addScenario = recommendationElement("button", "secondary-button", "Add comparison scenario");
    addScenario.type = "button";
    addScenario.addEventListener("click", () => {
      let number = 1;
      while (recommendationState.scenarios.some(scenario =>
        scenario.scenario_id === `scenario-${number}`)) number++;
      recommendationState.scenarios.push({
        scenario_id: `scenario-${number}`,
        label: `Scenario ${number}`,
        choices: {}
      });
      hideOldRecommendationResults();
      renderRecommendationStep();
    });
    panel.append(addScenario);
  } else if (activeRecommendationStep === "preference") {
    panel.append(recommendationElement("h3", "step-question", "Review your choices and recommendation style"));
    const review = recommendationElement("div", "recommendation-review");
    const current = Number(recommendationState.currentSemester);
    const target = Number(recommendationState.targetSemester);
    const lines = [
      `Current semester: ${current}`,
      `Specialization: ${recommendationState.specialization}`,
      `Target semester: ${target}`
    ];
    if (current >= 4) {
      let previousDescription = "Unknown or unanswered";
      if (recommendationState.previousState === "none" ||
          recommendationState.previousState === "none_reported") {
        previousDescription = "No elective reported";
      } else if (recommendationState.previousElectiveId) {
        const course = recommendationElectives.find(item =>
          String(item.course_id) === String(recommendationState.previousElectiveId));
        previousDescription = course ? fullElectiveLabel(course) : "Course unavailable";
      }
      lines.push(`Previous semester: ${previousDescription}`);
    }
    if (target > current) {
      const summaries = recommendationState.scenarios.map((scenario, index) => {
        const choices = Object.entries(scenario.choices).map(([semester, id]) => {
          const course = recommendationElectives.find(item => String(item.course_id) === String(id));
          return course ? `Semester ${semester}: ${course.code}` : `Semester ${semester}: undecided`;
        });
        return `${String.fromCharCode(65 + index)}. ${scenario.label || `Scenario ${index + 1}`} ` +
          (choices.length ? `(${choices.join("; ")})` : "(no hypothetical electives selected)");
      });
      lines.push(...summaries);
    }
    lines.forEach(line => review.append(recommendationElement("p", "", line)));
    panel.append(review);

    const label = recommendationElement("label", "wizard-field preference-control");
    label.append(recommendationElement("span", "field-label", "Recommendation style"));
    const select = recommendationElement("select", "wizard-select");
    select.id = "guided-preference";
    ["Easy-going", "Balanced", "Challenge me"].forEach(value =>
      select.append(new Option(value, value)));
    select.value = recommendationState.preference;
    select.addEventListener("change", () => {
      recommendationState.preference = select.value;
      hideOldRecommendationResults();
    });
    label.append(select);
    panel.append(label);
  }
}

function validateRecommendationStep() {
  if (activeRecommendationStep === "current" && !recommendationState.currentSemester) {
    setRecommendationMessage("Choose your current semester to continue.", true);
    return false;
  }
  if (activeRecommendationStep === "specialization" && !recommendationState.specialization) {
    setRecommendationMessage("Choose your specialization to continue.", true);
    return false;
  }
  if (activeRecommendationStep === "previous" && recommendationState.previousState === "selected" &&
      !recommendationState.previousElectiveId) {
    setRecommendationMessage("Select the previous-semester elective or choose another answer.", true);
    return false;
  }
  if (activeRecommendationStep === "target" && !recommendationState.targetSemester) {
    setRecommendationMessage("Choose a target semester with applicable electives.", true);
    return false;
  }
  return true;
}

function recommendationRequestBody() {
  const current = Number(recommendationState.currentSemester);
  const target = Number(recommendationState.targetSemester);
  let previous = null;
  if (current >= 4) {
    if (recommendationState.previousState === "none" ||
        recommendationState.previousState === "none_reported") previous = [];
    else if (recommendationState.previousElectiveId) {
      previous = [Number(recommendationState.previousElectiveId)];
    }
  }
  const body = {
    current_semester: current,
    specialization: recommendationState.specialization,
    target_semester: target,
    previous_electives: previous,
    preference: recommendationState.preference
  };
  if (target > current) {
    body.scenarios = recommendationState.scenarios.map((scenario, index) => ({
      scenario_id: scenario.scenario_id,
      label: scenario.label.trim() || `Scenario ${index + 1}`,
      course_ids: Object.entries(scenario.choices)
        .sort(([left], [right]) => Number(left) - Number(right))
        .map(([, courseId]) => Number(courseId))
    }));
  }
  return body;
}

function courseSummaryText(course) {
  if (!course) return "None";
  const identity = course.course_identity || {};
  return `Sem ${identity.semester} · ${course.course_code} — ${course.course_name} ` +
    `[${identity.specialization}; ${identity.course_group}]`;
}

function appendRecommendationList(parent, title, values, emptyText = "None") {
  const section = recommendationElement("div", "recommendation-detail");
  section.append(recommendationElement("strong", "", `${title}:`));
  const list = recommendationElement("ul", "recommendation-detail-list");
  if (values.length) {
    values.forEach(value => list.append(recommendationElement("li", "", value)));
  } else {
    list.append(recommendationElement("li", "muted-list-item", emptyText));
  }
  section.append(list);
  parent.append(section);
}

function renderBackgroundAssumptions(scenario) {
  const assumptions = scenario.assumptions || {};
  const details = recommendationElement("details", "scenario-assumptions");
  details.append(recommendationElement("summary", "", "Background assumptions and unknowns"));
  const content = recommendationElement("div", "assumption-groups");
  const groups = [
    ["Assumed completed MAIN courses", assumptions.assumed_main_courses || []],
    ["Ongoing current-semester MAIN courses (not counted)",
      assumptions.ongoing_current_semester_main_courses || []],
    ["Actual completed electives", assumptions.actual_completed_electives || []],
    ["Previous electives reported for this recommendation", assumptions.reported_previous_electives || []],
    ["Hypothetical planned electives", assumptions.hypothetical_planned_electives || []]
  ];
  groups.forEach(([label, courses]) => {
    const values = courses.map(courseSummaryText);
    appendRecommendationList(content, label, values);
  });
  const unknown = assumptions.unknown_intervening_semesters || [];
  appendRecommendationList(content, "Undecided intervening semesters",
    unknown.map(semester => `Semester ${semester}`), "None");
  (assumptions.warnings || []).forEach(warning =>
    content.append(recommendationElement("p", "recommendation-warning", warning)));
  details.append(content);
  return details;
}

function renderRecommendationCard(recommendation, rank) {
  const card = recommendationElement("article",
    `recommendation-card${recommendation.is_placeholder ? " placeholder" : ""}`);
  const header = recommendationElement("div", "recommendation-card-header");
  const title = recommendationElement("h4", "", `${recommendation.code} — ${recommendation.name}`);
  const rankBadge = recommendationElement("span", "rank-badge", `#${rank}`);
  header.append(title, rankBadge);
  card.append(header);

  const identity = recommendation.course_identity || {};
  const category = recommendation.elective_category ? ` · ${recommendation.elective_category}` : "";
  card.append(recommendationElement("p", "recommendation-meta",
    `Semester ${identity.semester} · ${recommendation.credits} credits · ` +
    `${recommendation.specialization || identity.specialization || "Specialization not provided"}${category}`));

  const difficulty = recommendationElement("div", "difficulty-summary");
  if (recommendation.base_difficulty == null) {
    difficulty.append(recommendationElement("span", "difficulty-chip unscored", "Base difficulty not provided"));
  } else {
    difficulty.append(recommendationElement("span", "difficulty-chip",
      `Base ${Number(recommendation.base_difficulty).toFixed(1)}`));
  }
  if (recommendation.personalized_difficulty == null) {
    const label = recommendation.is_placeholder ? "Placeholder · not scored" : "Personalized score unavailable";
    difficulty.append(recommendationElement("span", "difficulty-chip unscored", label));
  } else {
    difficulty.append(recommendationElement("span", "difficulty-chip personalized",
      `Personalized ${Number(recommendation.personalized_difficulty).toFixed(1)}`));
  }
  difficulty.append(recommendationElement("span", "difficulty-level",
    `Confidence: ${recommendation.difficulty_confidence || "Unknown"}`));
  card.append(difficulty);
  if (recommendation.difficulty_level) {
    card.append(recommendationElement("p", "recommendation-meta",
      `Catalog difficulty level: ${recommendation.difficulty_level}`));
  }

  appendRecommendationList(card, "Verified knowledge matches",
    recommendation.matched_knowledge_areas || []);
  appendRecommendationList(card, "Knowledge gaps",
    recommendation.unmatched_background_areas || []);
  appendRecommendationList(card, "Unknown or unmapped areas",
    recommendation.unknown_background_areas || []);

  const contributions = recommendation.background_contributions_by_source || {};
  const sourceLabels = [
    ["assumed_main", "Assumed MAIN courses"],
    ["actual_completed_elective", "Actual completed electives"],
    ["reported_previous_elective", "Previous electives reported"],
    ["hypothetical_planned_elective", "Hypothetical planned electives"]
  ];
  const details = recommendationElement("details", "recommendation-background");
  details.append(recommendationElement("summary", "", "Courses supporting matched areas"));
  const contributionContent = recommendationElement("div", "assumption-groups");
  sourceLabels.forEach(([source, label]) => {
    const providers = contributions[source] || [];
    const byCourse = new Map();
    providers.forEach(provider => {
      const key = provider.course_id;
      if (!byCourse.has(key)) byCourse.set(key, {provider, areas: new Set()});
      byCourse.get(key).areas.add(provider.knowledge_area);
    });
    const values = [...byCourse.values()].map(({provider, areas}) =>
      `${provider.course_code} — ${provider.course_name} (matched: ${[...areas].join(", ")})`);
    appendRecommendationList(contributionContent, label, values);
  });
  details.append(contributionContent);
  card.append(details);

  card.append(recommendationElement("p", "recommendation-explanation",
    recommendation.explanation || "No explanation was provided."));
  (recommendation.warnings || []).forEach(warning =>
    card.append(recommendationElement("p", "recommendation-warning", warning)));
  return card;
}

function renderRecommendationResults(data) {
  const results = document.querySelector("#recommendation-results");
  const title = document.querySelector("#recommendation-results-title");
  const preference = document.querySelector("#recommendation-results-preference");
  const scenariosContainer = document.querySelector("#recommendation-scenarios");
  scenariosContainer.replaceChildren();
  title.textContent = `Semester ${data.target_semester} elective recommendations`;
  preference.textContent = data.preference;

  if (!data.has_electives) {
    scenariosContainer.append(recommendationElement("p", "recommendation-empty",
      data.no_electives_reason || "No applicable electives are available for this selection."));
  }
  (data.scenarios || []).forEach(scenario => {
    const section = recommendationElement("section", "recommendation-scenario");
    const heading = recommendationElement("div", "scenario-result-heading");
    heading.append(recommendationElement("h3", "", scenario.label || scenario.scenario_id));
    heading.append(recommendationElement("span", "scenario-id", scenario.scenario_id));
    section.append(heading, renderBackgroundAssumptions(scenario));
    const recommendations = scenario.recommendations || [];
    if (!recommendations.length) {
      section.append(recommendationElement("p", "recommendation-empty",
        "No available electives match this semester and specialization."));
    } else {
      const cards = recommendationElement("div", "recommendation-list");
      recommendations.forEach((recommendation, index) =>
        cards.append(renderRecommendationCard(recommendation, index + 1)));
      section.append(cards);
    }
    scenariosContainer.append(section);
  });
  results.hidden = false;
}

async function submitSemesterRecommendations(body, {refresh = false} = {}) {
  const generateButton = document.querySelector("#recommendation-generate");
  if (!refresh) {
    hideOldRecommendationResults();
    generateButton.disabled = true;
    generateButton.textContent = "Loading recommendations…";
    setRecommendationMessage("Calculating recommendations for your selected semester…");
  } else {
    setRecommendationMessage("Refreshing recommendations after the saved course status changed…");
  }
  const requestNumber = ++recommendationRequestNumber;
  try {
    const response = await fetch("/api/recommendations/semester", {
      method: "POST",
      headers: {"Content-Type": "application/json"},
      body: JSON.stringify(body)
    });
    let data;
    try {
      data = await response.json();
    } catch (error) {
      throw new Error("The recommendation service returned an unexpected response. Please restart the current Flask application and try again.");
    }
    if (!response.ok) throw new Error(data.error || "Recommendations could not be loaded.");
    if (requestNumber !== recommendationRequestNumber) return;
    renderRecommendationResults(data);
    if (!refresh) lastGeneratedRecommendationRequest = body;
    const count = (data.scenarios || []).reduce((total, scenario) =>
      total + (scenario.recommendations || []).length, 0);
    setRecommendationMessage(
      data.has_electives
        ? `${count} target-semester recommendation${count === 1 ? "" : "s"} shown across ${data.scenarios.length} scenario${data.scenarios.length === 1 ? "" : "s"}.`
        : data.no_electives_reason || "No applicable electives are available."
    );
  } catch (error) {
    if (requestNumber !== recommendationRequestNumber) return;
    setRecommendationMessage(error.message, true);
    const results = document.querySelector("#recommendation-results");
    const container = document.querySelector("#recommendation-scenarios");
    container.replaceChildren(recommendationElement("p", "recommendation-empty error", error.message));
    results.hidden = false;
  } finally {
    if (!refresh) {
      generateButton.disabled = Boolean(recommendationCatalogError);
      generateButton.textContent = "Generate recommendations";
    }
  }
}

async function generateSemesterRecommendations() {
  if (!validateRecommendationStep()) return;
  const body = recommendationRequestBody();
  await submitSemesterRecommendations(body);
}

async function refreshLastRecommendations() {
  if (lastGeneratedRecommendationRequest) {
    await submitSemesterRecommendations(lastGeneratedRecommendationRequest, {refresh: true});
  }
}

function advanceRecommendationStep() {
  if (!validateRecommendationStep()) return;
  const steps = availableRecommendationSteps();
  const index = steps.indexOf(activeRecommendationStep);
  activeRecommendationStep = steps[Math.min(index + 1, steps.length - 1)];
  renderRecommendationStep();
  setRecommendationMessage("Review each step. Your assumptions and plans are temporary and will not be saved.");
}

function goBackRecommendationStep() {
  const steps = availableRecommendationSteps();
  const index = steps.indexOf(activeRecommendationStep);
  if (index > 0) activeRecommendationStep = steps[index - 1];
  renderRecommendationStep();
  setRecommendationMessage("Your earlier choices have been kept.");
}

async function loadRecommendationElectives() {
  try {
    const response = await fetch("/api/courses?type=ELECTIVE");
    if (!response.ok) throw new Error("Elective choices could not be loaded.");
    recommendationElectives = (await response.json()).filter(course =>
      course.course_type === "ELECTIVE");
    recommendationCatalogError = "";
    recommendationCatalogLoaded = true;
    updateDependentRecommendationState();
    renderRecommendationStep();
    setRecommendationMessage("Start with your current semester. Previous elective questions are skipped for Semesters 1–3.");
  } catch (error) {
    recommendationCatalogError = error.message;
    renderRecommendationStep();
    setRecommendationMessage(`${error.message} Check the local application and retry.`, true);
    const retry = recommendationElement("button", "secondary-button", "Retry elective list");
    retry.type = "button";
    retry.addEventListener("click", loadRecommendationElectives);
    document.querySelector("#recommendation-step").append(retry);
  }
}

function showEmptyState() {
  const row = document.createElement("tr");
  row.className = "empty-row";
  const cell = document.createElement("td");
  cell.colSpan = 8;
  const panel = document.createElement("div");
  panel.className = "empty-state";
  panel.innerHTML = "<span class=\"empty-icon\" aria-hidden=\"true\"><svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"M4 5.5A2.5 2.5 0 0 1 6.5 3H20v16H6.5A2.5 2.5 0 0 0 4 21z\"/><path d=\"M4 5.5v13A2.5 2.5 0 0 1 6.5 16H20\"/><path d=\"M8 7h8M8 10h6\"/></svg></span><h3>Your catalog is not ready yet</h3><p>Course records will appear here after the catalog has been finalized and imported. Your current catalog data has not been loaded.</p>";
  cell.append(panel);
  row.append(cell);
  document.querySelector("#courses").replaceChildren(row);
}

function courseIdentityLabel(course) {
  return `Sem ${course.semester} ${course.code} — ${course.name} ` +
    `[${course.course_type}; ${course.specialization}; ${course.course_group}]`;
}

const courseProfileSections = [
  ["Short description", "short_description"],
  ["Detailed description", "detailed_description"],
  ["Learning outcomes", "learning_outcomes"],
  ["Important topics", "important_topics"],
  ["Technical skills", "technical_skills"],
  ["Practical applications", "practical_applications"],
  ["Primary domain", "primary_domain"],
  ["Secondary domains", "secondary_domains"],
  ["Programming intensity", "programming_intensity"],
  ["Mathematical intensity", "mathematical_intensity"],
  ["Theory/practical orientation", "theory_practical_orientation"],
  ["Expected background", "expected_background"],
  ["Career domains", "career_domains"],
  ["Potential career roles", "potential_career_roles"],
  ["Future learning", "future_learning"],
  ["Source reference", "source_reference"]
];

function addProfileSection(parent, title, value) {
  const section = document.createElement("section");
  section.className = "course-profile-section";
  const heading = document.createElement("h3");
  heading.textContent = title;
  const description = document.createElement("p");
  description.textContent = value || "Not provided in the profile source.";
  section.append(heading, description);
  parent.append(section);
}

function renderCourseProfile(profile) {
  const title = document.querySelector("#course-profile-title");
  const content = document.querySelector("#course-profile-content");
  title.textContent = `${profile.course_code} — ${profile.course_name}`;
  content.replaceChildren();

  const metadata = document.createElement("p");
  metadata.className = "course-profile-meta";
  metadata.textContent = `Semester ${profile.semester} · ${profile.course_type} · ${profile.specialization} · ${profile.group}`;
  content.append(metadata);

  const status = document.createElement("p");
  status.className = "course-profile-status";
  status.textContent = `Verification status: ${profile.verification_status}`;
  content.append(status);

  if (profile.verification_status === "PLACEHOLDER_NO_COURSE") {
    const placeholderNote = document.createElement("p");
    placeholderNote.className = "course-profile-placeholder";
    placeholderNote.textContent = "This catalog entry is a placeholder slot and does not represent a named course.";
    content.append(placeholderNote);
  }

  courseProfileSections.forEach(([label, field]) =>
    addProfileSection(content, label, profile[field]));
}

async function loadCourseProfile(course, triggerButton, openDialog = true) {
  const dialog = document.querySelector("#course-profile-dialog");
  const title = document.querySelector("#course-profile-title");
  const content = document.querySelector("#course-profile-content");
  if (openDialog) {
    courseProfileReturnFocus = triggerButton;
    title.textContent = `${course.code} — Course details`;
    content.replaceChildren(recommendationElement("p", "course-profile-loading", "Loading course profile…"));
    dialog.showModal();
  } else {
    content.replaceChildren(recommendationElement("p", "course-profile-loading", "Loading course profile…"));
  }

  try {
    const response = await fetch(`/api/courses/${course.course_id}/profile`);
    const profile = await response.json();
    if (!response.ok) throw new Error(profile.error || "Course profile could not be loaded.");
    renderCourseProfile(profile);
  } catch (error) {
    content.replaceChildren();
    const message = recommendationElement("p", "course-profile-error", error.message);
    const retry = recommendationElement("button", "secondary-button", "Retry");
    retry.type = "button";
    retry.addEventListener("click", () => loadCourseProfile(course, triggerButton, false));
    content.append(message, retry);
  }
}

function initialPrerequisiteLabel(course) {
  const status = course.rule_status;
  if (status === "NO_PREREQUISITE_LISTED") return "No prerequisite listed (not officially verified)";
  if (status === "CONFIRMED_NO_PREREQUISITE") return "Confirmed: no prerequisite";
  if (status === "CONFIRMED_REQUIREMENTS") return `Confirmed requirements: ${course.prerequisite_text || "See check"}`;
  if (status === "PENDING_REVIEW") return `Pending review: ${course.prerequisite_text || "Source requires review"}`;
  return `Unresolved: ${course.prerequisite_text || "No prerequisite details available"}`;
}

function prerequisiteResultLabel(data) {
  let label = (data.evaluation_state || "UNRESOLVED").replaceAll("_", " ");
  if (data.evaluation_state === "NO_PREREQUISITE_LISTED") {
    label = "No prerequisite listed; eligibility not officially confirmed";
  } else if (data.evaluation_state === "NO_PREREQUISITE_CONFIRMED") {
    label = "Confirmed no prerequisite";
  }

  const incomplete = (data.incomplete_courses || []).map(course =>
    `${courseIdentityLabel(course)} (${course.status})`
  );
  if (incomplete.length) label += ` · Incomplete options: ${incomplete.join(", ")}`;

  const reasons = (data.reasons || []).map(reason => reason.message || reason.code);
  if (reasons.length) label += ` · ${reasons.join("; ")}`;

  const cycles = (data.cycle || []).map(courseIdentityLabel);
  if (cycles.length) label += ` · Cycle: ${cycles.join(" → ")}`;

  if (data.evaluation_state === "SATISFIED") label += " · Eligible under confirmed rules";
  if (data.evaluation_state === "INCOMPLETE") label += " · Not eligible under confirmed rules";
  return label;
}

function addCourseRow(course) {
  const row = document.createElement("tr");
  const courseCell = document.createElement("td");
  courseCell.textContent = `${course.code} — ${course.name}`;
  row.append(courseCell);

  [course.semester, course.credits].forEach(value => {
    const cell = document.createElement("td");
    cell.textContent = value;
    row.append(cell);
  });

  const typeCell = document.createElement("td");
  const typeBadge = document.createElement("span");
  typeBadge.className = `course-type${course.course_type === "ELECTIVE" ? " elective" : ""}`;
  typeBadge.textContent = course.course_type === "ELECTIVE" && course.elective_category
    ? `${course.course_type} · ${course.elective_category}`
    : course.course_type;
  typeCell.append(typeBadge);
  row.append(typeCell);

  const statusCell = document.createElement("td");
  const statusSelect = document.createElement("select");
  statusSelect.className = "status-select";
  statusSelect.setAttribute("aria-label", `Status for ${course.code}`);
  statusOptions.forEach(status => {
    const option = document.createElement("option");
    option.value = status;
    option.textContent = status.replaceAll("_", " ");
    option.selected = (course.status || "NOT_STARTED") === status;
    statusSelect.append(option);
  });
  statusSelect.addEventListener("change", async () => {
    const previousStatus = course.status || "NOT_STARTED";
    statusSelect.disabled = true;
    try {
      const response = await fetch(`/api/courses/${course.course_id}/status`, {
        method: "PUT",
        headers: {"Content-Type": "application/json"},
        body: JSON.stringify({status: statusSelect.value})
      });
      if (!response.ok) {
        const result = await response.json();
        throw new Error(result.error || "The course status could not be saved.");
      }
      course.status = statusSelect.value;
      setMessage(`Status saved for ${course.code}.`);
      await loadDashboard();
      await refreshLastRecommendations();
    } catch (error) {
      statusSelect.value = previousStatus;
      setMessage(error.message, true);
    } finally {
      statusSelect.disabled = false;
    }
  });
  statusCell.append(statusSelect);
  row.append(statusCell);

  const prerequisiteCell = document.createElement("td");
  prerequisiteCell.className = "prerequisite-text";
  const prerequisiteDescription = document.createElement("span");
  prerequisiteDescription.textContent = initialPrerequisiteLabel(course);
  const checkButton = document.createElement("button");
  checkButton.className = "prerequisite-check";
  checkButton.type = "button";
  checkButton.textContent = "Check";
  checkButton.setAttribute("aria-label", `Check prerequisites for ${course.code}`);
  checkButton.addEventListener("click", async () => {
    checkButton.disabled = true;
    try {
      const response = await fetch(`/api/courses/${course.course_id}/prerequisites`);
      const data = await response.json();
      if (!response.ok) throw new Error(data.error || "Prerequisite check failed.");
      prerequisiteDescription.textContent = prerequisiteResultLabel(data);
      setMessage(`Prerequisite check completed for ${course.code}.`);
    } catch (error) {
      prerequisiteDescription.textContent = error.message;
      setMessage(error.message, true);
    } finally {
      checkButton.disabled = false;
    }
  });
  prerequisiteCell.append(prerequisiteDescription, checkButton);
  row.append(prerequisiteCell);

  const detailsCell = document.createElement("td");
  const detailsButton = document.createElement("button");
  detailsButton.className = "course-details-button";
  detailsButton.type = "button";
  detailsButton.textContent = "View details";
  detailsButton.setAttribute("aria-label", `View profile for ${courseIdentityLabel(course)}`);
  detailsButton.addEventListener("click", () => loadCourseProfile(course, detailsButton));
  detailsCell.append(detailsButton);
  row.append(detailsCell);

  const planCell = document.createElement("td");
  const planCheckbox = document.createElement("input");
  planCheckbox.className = "plan-check";
  planCheckbox.type = "checkbox";
  planCheckbox.checked = selectedPlanCourseIds.has(course.course_id);
  planCheckbox.setAttribute("aria-label", `Add ${course.code} to the hypothetical plan`);
  planCheckbox.addEventListener("change", () => {
    if (planCheckbox.checked && course.course_type === "ELECTIVE") {
      if (course.semester === 8 && !course.elective_category) {
        planCheckbox.checked = false;
        setMessage(
          `Semester 8 elective ${course.code} has no PEC/OEC category and cannot be added yet.`,
          true
        );
        return;
      }

      const conflictingElective = Array.from(selectedPlanCourseIds).some(courseId => {
        const selectedCourse = selectedPlanCourseDetails.get(courseId);
        if (!selectedCourse || selectedCourse.course_type !== "ELECTIVE" ||
            selectedCourse.semester !== course.semester) return false;
        if (course.semester === 8) {
          return selectedCourse.elective_category === course.elective_category;
        }
        return true;
      });
      if (conflictingElective) {
        planCheckbox.checked = false;
        const limit = course.semester === 8
          ? `Only one ${course.elective_category} elective can be selected for Semester 8.`
          : `Only one elective can be selected for Semester ${course.semester}.`;
        setMessage(`${limit} Remove the selected elective first.`, true);
        return;
      }
    }

    if (planCheckbox.checked) {
      selectedPlanCourseIds.add(course.course_id);
      selectedPlanCourseDetails.set(course.course_id, {
        semester: course.semester,
        course_type: course.course_type,
        elective_category: course.elective_category || null
      });
    } else {
      selectedPlanCourseIds.delete(course.course_id);
      selectedPlanCourseDetails.delete(course.course_id);
    }
    updatePlannerSelection();
  });
  planCell.append(planCheckbox);
  row.append(planCell);
  document.querySelector("#courses").append(row);
}

function updatePlannerSelection() {
  const count = selectedPlanCourseIds.size;
  document.querySelector("#planner-selection-count").textContent =
    `${count} ${count === 1 ? "course" : "courses"} selected`;
  document.querySelector("#preview-plan").disabled = count === 0;
  document.querySelector("#planner-result").hidden = true;
}

function showPlannerResult(data) {
  const result = document.querySelector("#planner-result");
  result.replaceChildren();
  result.classList.remove("error");
  const summary = document.createElement("p");
  summary.textContent = `Hypothetical totals: ${data.totals.total_credits} total credits · ${data.totals.compulsory_credits} compulsory · ${data.totals.elective_option_credits} elective-option credits`;
  const courses = document.createElement("p");
  courses.textContent = `Selected: ${data.courses.map(course => course.code).join(", ") || "none"}`;
  const note = document.createElement("p");
  note.textContent = data.notice;
  result.append(summary, courses, note);
  result.hidden = false;
}

async function previewPlan() {
  const result = document.querySelector("#planner-result");
  const button = document.querySelector("#preview-plan");
  button.disabled = true;
  try {
    const response = await fetch("/api/planner/preview", {
      method: "POST",
      headers: {"Content-Type": "application/json"},
      body: JSON.stringify({course_ids: Array.from(selectedPlanCourseIds)})
    });
    const data = await response.json();
    if (!response.ok) throw new Error(data.error || "The plan could not be previewed.");
    showPlannerResult(data);
  } catch (error) {
    result.textContent = error.message;
    result.classList.add("error");
    result.hidden = false;
  } finally {
    button.disabled = selectedPlanCourseIds.size === 0;
  }
}

async function loadCourses() {
  const params = new URLSearchParams();
  const filters = {
    search: document.querySelector("#search").value.trim(),
    semester: document.querySelector("#semester").value,
    type: document.querySelector("#type").value,
    specialization: document.querySelector("#specialization").value.trim(),
    group: document.querySelector("#group").value.trim()
  };
  Object.entries(filters).forEach(([name, value]) => {
    if (value) params.set(name, value);
  });

  try {
    const response = await fetch(`/api/courses?${params}`);
    if (!response.ok) throw new Error("The course list could not be loaded.");
    const courses = await response.json();
    const body = document.querySelector("#courses");
    const count = document.querySelector("#catalog-count");
    count.textContent = `${courses.length} ${courses.length === 1 ? "course" : "courses"}`;
    body.replaceChildren();
    if (courses.length === 0) {
      showEmptyState();
      setMessage("The catalog is empty until finalized course data is imported.");
      return;
    }
    setMessage(`${courses.length} course${courses.length === 1 ? "" : "s"} shown.`);
    courses.forEach(addCourseRow);
  } catch (error) {
    document.querySelector("#catalog-count").textContent = "Unavailable";
    document.querySelector("#courses").replaceChildren();
    setMessage(error.message, true);
  }
}

function showPriorityMessage(message, isError = false) {
  const messageElement = document.querySelector("#priority-message");
  messageElement.textContent = message;
  messageElement.classList.toggle("error", isError);
}

async function loadSubjectPriority() {
  const semester = document.querySelector("#priority-semester").value;
  const tableWrap = document.querySelector("#priority-table-wrap");
  const tableBody = document.querySelector("#priority-courses");
  tableWrap.hidden = true;
  tableBody.replaceChildren();
  if (!semester) {
    showPriorityMessage("Choose a semester to view subject priority.");
    return;
  }

  showPriorityMessage(`Loading Semester ${semester} subjects…`);
  try {
    const response = await fetch(`/api/subject-priority?semester=${encodeURIComponent(semester)}`);
    const data = await response.json();
    if (!response.ok) throw new Error(data.error || "Subject priority could not be loaded.");
    if (data.courses.length === 0) {
      showPriorityMessage(`No subjects are available for Semester ${semester}.`);
      return;
    }

    data.courses.forEach(course => {
      const row = document.createElement("tr");
      const values = [
        course.rank === null ? "—" : String(course.rank),
        course.code,
        course.name,
        course.course_type === "ELECTIVE" && course.elective_category
          ? `${course.course_type} · ${course.elective_category}`
          : course.course_type,
        course.credits === null ? "Unavailable" : `${course.credits} credits`
      ];
      values.forEach((value, index) => {
        const cell = document.createElement("td");
        cell.textContent = value || "—";
        if (index === 0) cell.className = "priority-rank";
        if (index === 1) cell.className = "priority-code";
        row.append(cell);
      });

      const weightCell = document.createElement("td");
      weightCell.className = "priority-weight-cell";
      if (!course.credit_data_valid) {
        weightCell.textContent = course.data_warning || "Credit weight unavailable.";
      } else if (course.credit_weight_percent === null) {
        weightCell.textContent = "Unavailable (semester total is zero)";
      } else {
        const label = document.createElement("span");
        label.className = "priority-weight-label";
        label.textContent = `${course.credit_weight_percent.toFixed(1)}%`;
        const track = document.createElement("span");
        track.className = "priority-bar-track";
        const bar = document.createElement("span");
        bar.className = "priority-bar";
        bar.style.width = `${Math.max(0, Math.min(100, course.credit_weight_percent))}%`;
        track.setAttribute("role", "progressbar");
        track.setAttribute("aria-label", `${course.code} credit weight`);
        track.setAttribute("aria-valuemin", "0");
        track.setAttribute("aria-valuemax", "100");
        track.setAttribute("aria-valuenow", String(course.credit_weight_percent));
        track.append(bar);
        weightCell.append(label, track);
      }
      row.append(weightCell);
      tableBody.append(row);
    });

    tableWrap.hidden = false;
    const invalidCount = data.invalid_credit_course_count || 0;
    showPriorityMessage(
      `${data.courses.length} subject${data.courses.length === 1 ? "" : "s"} shown · ` +
      `${data.total_semester_credits} valid listed credits` +
      (invalidCount ? ` · ${invalidCount} course${invalidCount === 1 ? "" : "s"} with invalid credits are unranked.` : ".")
    );
  } catch (error) {
    tableBody.replaceChildren();
    showPriorityMessage(error.message || "Subject priority is unavailable.", true);
  }
}

for (let semester = 1; semester <= 8; semester++) {
  const option = document.createElement("option");
  option.value = semester;
  option.textContent = `Semester ${semester}`;
  document.querySelector("#semester").append(option);

  const priorityOption = document.createElement("option");
  priorityOption.value = String(semester);
  priorityOption.textContent = `Semester ${semester}`;
  document.querySelector("#priority-semester").append(priorityOption);
}

document.querySelector("#filters").addEventListener("submit", event => event.preventDefault());
document.querySelector("#preview-plan").addEventListener("click", previewPlan);
document.querySelector("#priority-semester").addEventListener("change", loadSubjectPriority);
document.querySelector("#close-course-profile").addEventListener("click", () => {
  document.querySelector("#course-profile-dialog").close();
});
document.querySelector("#course-profile-dialog").addEventListener("close", () => {
  if (courseProfileReturnFocus && courseProfileReturnFocus.isConnected) {
    courseProfileReturnFocus.focus();
  }
  courseProfileReturnFocus = null;
});
document.querySelector("#course-profile-dialog").addEventListener("click", event => {
  if (event.target === event.currentTarget) event.currentTarget.close();
});
document.querySelector("#recommendation-continue").addEventListener("click", advanceRecommendationStep);
document.querySelector("#recommendation-back").addEventListener("click", goBackRecommendationStep);
document.querySelector("#recommendation-generate").addEventListener("click", generateSemesterRecommendations);
document.querySelectorAll("#search, #specialization, #group").forEach(control =>
  control.addEventListener("input", loadCourses));
document.querySelectorAll("#semester, #type").forEach(control =>
  control.addEventListener("change", loadCourses));

loadDashboard();
loadCourses();
loadRecommendationElectives();
