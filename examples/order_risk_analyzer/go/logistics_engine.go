// Logistics and final-decision policy compiled by polyc's built-in Go frontend.
package main

// LogisticsSession and its constructor/receiver methods come from the
// project-vendored logistics_policy source package. polyc resolves and merges
// that package before invoking its built-in Go frontend.
func logistics_session_decision(gate int, riskBand int, etaDays int) int {
	var session LogisticsSession
	NewLogisticsSession(&session, gate, riskBand, etaDays)
	session.AdjustETA(0)
	return session.Decision()
}

func logistics_base_days(zone int, requested int, reserved int) int {
	if reserved < requested {
		return 9
	} else {
		if zone >= 3 {
			if requested >= 8 {
				return 7
			} else {
			}
			return 5
		} else {
		}
	}

	if zone == 2 {
		if requested >= 8 {
			return 5
		} else {
		}
		return 4
	} else {
	}
	return 2
}

func logistics_capacity_delay(open_work_units int, daily_capacity int, priority int) int {
	if priority >= 2 {
		return 0
	} else {
		if open_work_units <= daily_capacity {
			return 1
		} else {
		}
	}

	if open_work_units <= daily_capacity+daily_capacity {
		return 2
	} else {
	}
	return 3
}

func logistics_decision(gate int, risk_band int, eta_days int) int {
	if gate == 40 {
		return 40
	} else {
		if gate == 30 {
			return 30
		} else {
			if gate == 20 {
				return 20
			} else {
			}
		}
	}

	if gate != 1 {
		return 50
	} else {
		if eta_days >= 10 {
			return 20
		} else {
		}
	}

	if risk_band >= 3 {
		return 40
	} else {
	}
	return 100 + risk_band*10 + eta_days
}
