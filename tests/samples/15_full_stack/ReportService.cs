// ============================================================================
// ReportService.cs — C# report generation service
// Compiled by PolyglotCompiler's frontend_dotnet → shared IR
// ============================================================================

using System;
using System.Text;

public class ReportService
{
    private readonly string _title;

    // Constructor
    public ReportService(string title)
    {
        _title = title;
    }

    // Render a simple HTML report from a JSON summary string
    public string RenderFromJson(string jsonSummary)
    {
        var sb = new StringBuilder();
        sb.AppendLine("<html>");
        sb.AppendLine($"<head><title>{_title}</title></head>");
        sb.AppendLine("<body>");
        sb.AppendLine($"<h1>{_title}</h1>");
        sb.AppendLine($"<pre>{jsonSummary}</pre>");
        sb.AppendLine("</body>");
        sb.AppendLine("</html>");
        return sb.ToString();
    }

    // Render a plain-text report
    public string RenderPlainText(string jsonSummary)
    {
        return $"=== {_title} ===\n{jsonSummary}\n===";
    }
}
