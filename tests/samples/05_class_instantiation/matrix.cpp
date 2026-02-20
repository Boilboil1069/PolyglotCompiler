// ============================================================================
// matrix.cpp — C++ Matrix class for class instantiation demo
// Compiled by PolyglotCompiler's frontend_cpp → shared IR
// ============================================================================

#include <vector>
#include <cmath>
#include <stdexcept>

class Matrix {
public:
    int rows;
    int cols;
    std::vector<std::vector<double>> data;

    // Constructor: create a rows x cols matrix filled with value
    Matrix(int r, int c, double value = 0.0)
        : rows(r), cols(c), data(r, std::vector<double>(c, value)) {}

    // Get element at (row, col)
    double get(int row, int col) const {
        return data[row][col];
    }

    // Set element at (row, col)
    void set(int row, int col, double value) {
        data[row][col] = value;
    }

    // Transpose the matrix
    Matrix transpose() const {
        Matrix result(cols, rows);
        for (int i = 0; i < rows; ++i) {
            for (int j = 0; j < cols; ++j) {
                result.data[j][i] = data[i][j];
            }
        }
        return result;
    }

    // Matrix multiplication
    Matrix multiply(const Matrix& other) const {
        if (cols != other.rows) {
            throw std::runtime_error("Dimension mismatch in matrix multiply");
        }
        Matrix result(rows, other.cols);
        for (int i = 0; i < rows; ++i) {
            for (int j = 0; j < other.cols; ++j) {
                for (int k = 0; k < cols; ++k) {
                    result.data[i][j] += data[i][k] * other.data[k][j];
                }
            }
        }
        return result;
    }

    // Frobenius norm
    double norm() const {
        double sum = 0.0;
        for (int i = 0; i < rows; ++i) {
            for (int j = 0; j < cols; ++j) {
                sum += data[i][j] * data[i][j];
            }
        }
        return std::sqrt(sum);
    }

    // Fill with sequential values for testing
    void fill_sequential() {
        int val = 1;
        for (int i = 0; i < rows; ++i) {
            for (int j = 0; j < cols; ++j) {
                data[i][j] = static_cast<double>(val++);
            }
        }
    }
};
