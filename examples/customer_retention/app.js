"use strict";

  var payload = window.CUSTOMER_RETENTION_DATA;
  var policy = window.js_retention_score;
  var segments = {
    1: { key: "healthy", label: "健康", description: "活跃稳定", color: "#9fd9c0" },
    2: { key: "watch", label: "关注", description: "需要培育", color: "#f4b95f" },
    3: { key: "risk", label: "风险", description: "优先触达", color: "#ef755d" }
  };

  var state = {
    filter: "all",
    query: "",
    sort: "health-desc"
  };

  function byId(id) {
    return document.getElementById(id);
  }

  function segmentFor(account) {
    return segments[account.segment] || segments[3];
  }

  function setText(id, value) {
    var element = byId(id);
    if (element) {
      element.textContent = String(value);
    }
  }

  function formatSigned(value) {
    if (value > 0) {
      return "+" + value;
    }
    return String(value);
  }

  function validateData(showConfirmation) {
    var validationLine = byId("validation-line");
    var copy = byId("validation-copy");
    var valid = Boolean(payload && Array.isArray(payload.accounts) && typeof policy == "function");

    if (valid) {
      valid = payload.accounts.length == 5 && payload.checksum == 45;
    }

    if (valid) {
      valid = payload.accounts.every(function (account) {
        return policy(account.activity, account.daysSinceLogin, payload.weights.inactivity) == account.health;
      });
    }

    if (valid) {
      valid = payload.accounts.reduce(function (sum, account) {
        return sum + account.health;
      }, 0) == payload.checksum;
    }

    validationLine.classList.remove("is-valid", "is-invalid");
    if (valid) {
      validationLine.classList.add("is-valid");
      if (showConfirmation) {
        copy.textContent = "校验完成：5 条评分与浏览器策略一致";
      } else {
        copy.textContent = "编译数据已加载，跨语言评分一致";
      }
    } else {
      validationLine.classList.add("is-invalid");
      copy.textContent = "数据校验失败，请先运行 ./run.sh 重新生成数据";
    }
    return valid;
  }

  function getCounts(accounts) {
    var counts = { healthy: 0, watch: 0, risk: 0 };
    accounts.forEach(function (account) {
      counts[segmentFor(account).key] += 1;
    });
    return counts;
  }

  function renderMetrics(accounts) {
    var counts = getCounts(accounts);
    var healthTotal = accounts.reduce(function (sum, account) { return sum + account.health; }, 0);
    var daysTotal = accounts.reduce(function (sum, account) { return sum + account.daysSinceLogin; }, 0);
    var average = 0;
    var averageDays = 0;
    if (accounts.length > 0) {
      average = healthTotal / accounts.length;
      averageDays = daysTotal / accounts.length;
    }

    setText("metric-total", accounts.length);
    setText("metric-average", average.toFixed(1));
    setText("metric-risk", counts.risk);
    setText("metric-days", averageDays.toFixed(0));
    setText("hero-risk-count", counts.risk);
    setText("hero-checksum", payload.checksum);
    if (average >= payload.thresholds.watch) {
      setText("average-caption", "整体仍在关注线以上");
    } else {
      setText("average-caption", "整体低于关注线");
    }
    setText("risk-caption", Math.round(counts.risk * 100 / accounts.length) + "% 的账户需要主动触达");
    setText("count-all", accounts.length);
    setText("count-healthy", counts.healthy);
    setText("count-watch", counts.watch);
    setText("count-risk", counts.risk);
  }

  function renderDistribution(accounts) {
    var counts = getCounts(accounts);
    var total = accounts.length;
    if (total == 0) {
      total = 1;
    }
    var healthyEnd = counts.healthy * 100 / total;
    var watchEnd = healthyEnd + counts.watch * 100 / total;
    var donut = byId("segment-donut");
    donut.style.background = "conic-gradient(" +
      segments[1].color + " 0 " + healthyEnd + "%, " +
      segments[2].color + " " + healthyEnd + "% " + watchEnd + "%, " +
      segments[3].color + " " + watchEnd + "% 100%)";
    donut.setAttribute(
      "aria-label",
      "客户健康分布：健康 " + counts.healthy + "，关注 " + counts.watch + "，风险 " + counts.risk
    );
    setText("donut-total", accounts.length);

    byId("segment-legend").innerHTML = [1, 2, 3].map(function (code) {
      var item = segments[code];
      var count = counts[item.key];
      return "<div class=\"legend-item\">" +
        "<span class=\"legend-swatch\" style=\"background:" + item.color + "\"></span>" +
        "<div class=\"legend-copy\"><strong>" + item.label + "</strong><small>" + item.description + "</small></div>" +
        "<span class=\"legend-value\">" + count + "</span>" +
        "</div>";
    }).join("");
  }

  function renderScoreBars(accounts) {
    byId("score-bars").innerHTML = accounts.map(function (account) {
      var segment = segmentFor(account);
      var width = account.health + 30;
      width = width * 100 / 80;
      if (width < 6) { width = 6; }
      if (width > 100) { width = 100; }
      return "<div class=\"score-row\">" +
        "<span>CR-" + account.id + "</span>" +
        "<div class=\"score-track\"><div class=\"score-fill\" style=\"width:" + width + "%;background:" + segment.color + "\"></div></div>" +
        "<span class=\"score-value\">" + formatSigned(account.health) + "</span>" +
        "</div>";
    }).join("");
  }

  function filteredAccounts() {
    var accounts = payload.accounts.filter(function (account) {
      var matchesFilter = state.filter == "all" || segmentFor(account).key == state.filter;
      var matchesQuery = String(account.id).indexOf(state.query) != -1;
      return matchesFilter && matchesQuery;
    }).slice();

    accounts.sort(function (left, right) {
      if (state.sort == "health-asc") { return left.health - right.health; }
      if (state.sort == "days-desc") { return right.daysSinceLogin - left.daysSinceLogin; }
      if (state.sort == "id-asc") { return left.id - right.id; }
      return right.health - left.health;
    });
    return accounts;
  }

  function renderTable() {
    var accounts = filteredAccounts();
    var rows = byId("account-rows");
    var empty = byId("empty-state");
    empty.hidden = accounts.length != 0;
    rows.innerHTML = accounts.map(function (account) {
      var segment = segmentFor(account);
      return "<tr>" +
        "<td><button class=\"account-link\" type=\"button\" data-account-id=\"" + account.id + "\">CR-" + account.id + "</button></td>" +
        "<td>" + account.sessions + " / " + account.purchases + "</td>" +
        "<td>" + account.complaints + "</td>" +
        "<td>" + account.daysSinceLogin + " 天</td>" +
        "<td>" + formatSigned(account.activity) + "</td>" +
        "<td><span class=\"score-number\">" + formatSigned(account.health) + "</span></td>" +
        "<td><span class=\"segment-pill segment-" + segment.key + "\">" + segment.label + "</span></td>" +
        "</tr>";
    }).join("");
  }

  function renderDetail(account) {
    var segment = segmentFor(account);
    var activityFormula = account.sessions + " × " + payload.weights.session + " + " +
      account.purchases + " × " + payload.weights.purchase + " − " +
      account.complaints + " × " + payload.weights.complaint;
    var healthFormula = account.activity + " − " + account.daysSinceLogin + " × " + payload.weights.inactivity;

    setText("detail-title", "CR-" + account.id + " / " + segment.label);
    byId("detail-content").innerHTML =
      "<div class=\"detail-score\"><div><small>最终健康分</small><strong>" + formatSigned(account.health) + "</strong></div>" +
      "<span class=\"segment-pill segment-" + segment.key + "\">" + segment.label + "</span></div>" +
      "<div class=\"trace-list\">" +
      "<div class=\"trace-step\"><b>PY</b><div><span>Python · 遥测生成</span><small>sessions / purchases / complaints / recency</small></div><strong>" + account.sessions + "·" + account.purchases + "·" + account.complaints + "·" + account.daysSinceLogin + "</strong></div>" +
      "<div class=\"trace-step\"><b>JV</b><div><span>Java · 活跃信号</span><small>权重化业务规则</small></div><strong>" + formatSigned(account.activity) + "</strong></div>" +
      "<div class=\"trace-step\"><b>JS</b><div><span>JavaScript · 留存策略</span><small>扣除未登录衰减</small></div><strong>" + formatSigned(account.health) + "</strong></div>" +
      "</div>" +
      "<div class=\"formula-card\">activity = " + activityFormula + " = " + account.activity + "<br />" +
      "health = " + healthFormula + " = " + account.health + "</div>";

    byId("drawer-backdrop").hidden = false;
    document.body.style.overflow = "hidden";
    byId("drawer-close").focus();
  }

  function closeDetail() {
    byId("drawer-backdrop").hidden = true;
    document.body.style.overflow = "";
  }

  function activateFilter(filter) {
    state.filter = filter;
    document.querySelectorAll("[data-filter]").forEach(function (button) {
      button.classList.toggle("is-active", button.getAttribute("data-filter") == filter);
    });
    renderTable();
  }

  function bindEvents() {
    document.querySelectorAll("[data-filter]").forEach(function (button) {
      button.addEventListener("click", function () {
        activateFilter(button.getAttribute("data-filter"));
      });
    });

    byId("account-search").addEventListener("input", function (event) {
      state.query = event.target.value.trim();
      renderTable();
    });

    byId("account-sort").addEventListener("change", function (event) {
      state.sort = event.target.value;
      renderTable();
    });

    byId("account-rows").addEventListener("click", function (event) {
      var button = event.target.closest("[data-account-id]");
      if (!button) { return; }
      var id = Number(button.getAttribute("data-account-id"));
      var account = payload.accounts.find(function (candidate) { return candidate.id == id; });
      if (account) { renderDetail(account); }
    });

    byId("risk-cta").addEventListener("click", function () {
      activateFilter("risk");
      byId("accounts").scrollIntoView({ behavior: "smooth", block: "start" });
    });

    byId("verify-button").addEventListener("click", function () {
      validateData(true);
    });

    byId("drawer-close").addEventListener("click", closeDetail);
    byId("drawer-backdrop").addEventListener("click", function (event) {
      if (event.target == byId("drawer-backdrop")) { closeDetail(); }
    });
    document.addEventListener("keydown", function (event) {
      if (event.key == "Escape" && !byId("drawer-backdrop").hidden) { closeDetail(); }
    });
  }

  function renderApp() {
    if (!payload || !Array.isArray(payload.accounts)) {
      byId("validation-line").classList.add("is-invalid");
      setText("validation-copy", "缺少编译数据，请先运行 ./run.sh");
      return;
    }
    renderMetrics(payload.accounts);
    renderDistribution(payload.accounts);
    renderScoreBars(payload.accounts);
    renderTable();
    validateData(false);
    bindEvents();
  }

  renderApp();
