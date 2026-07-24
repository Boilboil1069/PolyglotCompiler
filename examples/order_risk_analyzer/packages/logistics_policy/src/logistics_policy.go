// Vendored logistics domain package. polyc reads this source through
// poly.package.toml, rewrites the package boundary into the consumer's source
// unit, and sends the result to its built-in Go frontend.
package logistics_policy

// LogisticsSession owns the mutable state for one delivery decision.
// Go has no deterministic destructor; its executable object lifecycle here is
// stack allocation, explicit construction, pointer-receiver mutation, and
// receiver-method reads before the stack frame is released.
type LogisticsSession struct {
	gate     int
	riskBand int
	etaDays  int
}

func NewLogisticsSession(session *LogisticsSession, gate int, riskBand int, etaDays int) {
	session.gate = gate
	session.riskBand = riskBand
	session.etaDays = etaDays
}

func (session *LogisticsSession) AdjustETA(extraDays int) {
	session.etaDays += extraDays
}

func (session *LogisticsSession) Decision() int {
	if session.gate == 40 {
		return 40
	} else {
		if session.gate == 30 {
			return 30
		} else {
			if session.gate == 20 {
				return 20
			} else {
			}
		}
	}

	if session.gate != 1 {
		return 50
	} else {
		if session.etaDays >= 10 {
			return 20
		} else {
		}
	}

	if session.riskBand >= 3 {
		return 40
	} else {
	}
	return 100 + session.riskBand*10 + session.etaDays
}
