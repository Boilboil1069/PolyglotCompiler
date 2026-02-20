// ============================================================================
// Transformer.cs — C# data transformer using dictionaries
// Compiled by PolyglotCompiler's frontend_dotnet → shared IR
// ============================================================================

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

public class Transformer
{
    // Default constructor
    public Transformer() { }

    // Parse a JSON-like string into a dictionary (simplified stub)
    public Dictionary<string, string> ParseJson(string json)
    {
        var result = new Dictionary<string, string>();
        // Strip braces and quotes, split by commas
        var body = json.Trim().TrimStart('{').TrimEnd('}');
        foreach (var pair in body.Split(','))
        {
            var kv = pair.Split(':');
            if (kv.Length == 2)
            {
                var key = kv[0].Trim().Trim('"');
                var val = kv[1].Trim().Trim('"');
                result[key] = val;
            }
        }
        return result;
    }

    // Add or overwrite a key-value pair
    public Dictionary<string, string> Enrich(
        Dictionary<string, string> data, string key, string value)
    {
        data[key] = value;
        return data;
    }

    // Convert a dictionary back to a JSON-like string
    public string ToJsonString(Dictionary<string, string> data)
    {
        var sb = new StringBuilder();
        sb.Append('{');
        var entries = data.Select(kv => $"\"{kv.Key}\": \"{kv.Value}\"");
        sb.Append(string.Join(", ", entries));
        sb.Append('}');
        return sb.ToString();
    }

    // Flatten a nested array of doubles into a single array
    public double[] Flatten(double[] data)
    {
        // Already flat — identity operation for the demo
        return data;
    }
}
