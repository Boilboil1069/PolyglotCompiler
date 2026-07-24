// Classification primitives compiled by polyc's Go frontend.
package main

func tree_predict(petal_length int, petal_width int, setosa_threshold int, virginica_threshold int) int {
	if petal_length < setosa_threshold {
		return 0
	} else {
		if petal_width < virginica_threshold {
			return 1
		} else {
		}
	}
	return 2
}

func centroid_term(value int, class_sum int, class_count int) int {
	delta := class_count*value - class_sum
	return delta * delta
}

func centroid_choose(distance_zero int, distance_one int, distance_two int) int {
	if distance_zero <= distance_one {
		if distance_zero <= distance_two {
			return 0
		} else {
		}
	} else {
	}
	if distance_one <= distance_two {
		return 1
	} else {
	}
	return 2
}

func classification_error(predicted int, actual int, penalty int) int {
	if predicted == actual {
		return 0
	} else {
	}
	return penalty
}
