// ============================================================================
// DataService.cs — C# data service class for .NET interop demo
// Compiled by PolyglotCompiler's frontend_dotnet → shared IR
// ============================================================================

using System;
using System.Collections.Generic;
using System.Linq;

public class DataService
{
    private readonly string _name;
    private readonly List<double> _values;

    // Constructor
    public DataService(string name)
    {
        _name = name;
        _values = new List<double>();
    }

    // Add a value to the internal list
    public void AddValue(double value)
    {
        _values.Add(value);
    }

    // Compute the average of stored values
    public double Average()
    {
        if (_values.Count == 0) return 0.0;
        return _values.Average();
    }

    // Get the number of stored values
    public int Count()
    {
        return _values.Count;
    }

    // Find the maximum value
    public double Max()
    {
        if (_values.Count == 0) return 0.0;
        return _values.Max();
    }

    // Find the minimum value
    public double Min()
    {
        if (_values.Count == 0) return 0.0;
        return _values.Min();
    }

    // Format a summary string
    public string Summary()
    {
        return $"{_name}: count={_values.Count}, avg={Average():F2}, min={Min():F2}, max={Max():F2}";
    }

    // Static utility: clamp a value between min and max
    public static double Clamp(double value, double min, double max)
    {
        if (value < min) return min;
        if (value > max) return max;
        return value;
    }
}
