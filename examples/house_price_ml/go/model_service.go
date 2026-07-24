// Prediction and evaluation kernels compiled by the built-in Go frontend.
package main

func model_predict(slope int, intercept int, feature int) int {
	return slope*feature + intercept
}

func model_absolute_error(prediction int, target int, unused int) int {
	if prediction >= target {
		return prediction - target + unused
	} else {
	}
	return target - prediction + unused
}

func model_mean_absolute_error(total_error int, sample_count int, fallback int) int {
	if sample_count <= 0 {
		return fallback
	} else {
	}
	if total_error < 0 {
		return fallback
	} else {
	}

	// Quantized MAE is capped at eight price units in this first model domain.
	if total_error >= sample_count*8 {
		return 8
	} else {
	}
	if total_error >= sample_count*7 {
		return 7
	} else {
	}
	if total_error >= sample_count*6 {
		return 6
	} else {
	}
	if total_error >= sample_count*5 {
		return 5
	} else {
	}
	if total_error >= sample_count*4 {
		return 4
	} else {
	}
	if total_error >= sample_count*3 {
		return 3
	} else {
	}
	if total_error >= sample_count*2 {
		return 2
	} else {
	}
	if total_error >= sample_count {
		return 1
	} else {
	}
	return 0
}
