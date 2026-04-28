// Fixture: C# source that requires C# 11 (file-scoped types).
// `file class` was introduced in C# 11 alongside raw string literals.
// Both features must be rejected by the PolyglotCompiler .NET frontend
// when --cs-lang is older than 11.

namespace PolyglotCompiler.Tests.LanguageVersions;

file class RawStringInElevenInternal
{
    public static string Greeting => "Hello, raw-string world!";
}
