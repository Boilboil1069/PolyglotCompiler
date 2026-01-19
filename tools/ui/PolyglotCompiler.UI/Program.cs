using System;
using System.Drawing;
using System.Windows.Forms;

namespace PolyglotCompiler.UI
{
    public static class Program
    {
        [STAThread]
        public static void Main()
        {
            ApplicationConfiguration.Initialize();
            Application.Run(new MainForm());
        }
    }

    public class MainForm : Form
    {
        private readonly ComboBox _languageSelect = new();
        private readonly TextBox _sourceInput = new();
        private readonly Button _compileButton = new();
        private readonly TextBox _outputBox = new();

        public MainForm()
        {
            Text = "PolyglotCompiler UI";
            Width = 900;
            Height = 600;

            _languageSelect.Items.AddRange(new object[] { "C/C++", "Python", "Rust" });
            _languageSelect.SelectedIndex = 1;
            _languageSelect.Dock = DockStyle.Top;

            _sourceInput.Multiline = true;
            _sourceInput.Font = new Font(FontFamily.GenericMonospace, 11);
            _sourceInput.Height = 250;
            _sourceInput.Dock = DockStyle.Top;
            _sourceInput.Text = "print('hello')";

            _compileButton.Text = "Compile";
            _compileButton.Dock = DockStyle.Top;
            _compileButton.Height = 40;
            _compileButton.Click += (_, _) => RunCompilation();

            _outputBox.Multiline = true;
            _outputBox.Font = new Font(FontFamily.GenericMonospace, 10);
            _outputBox.ReadOnly = true;
            _outputBox.Dock = DockStyle.Fill;

            Controls.Add(_outputBox);
            Controls.Add(_compileButton);
            Controls.Add(_sourceInput);
            Controls.Add(_languageSelect);
        }

        private void RunCompilation()
        {
            var language = _languageSelect.SelectedItem?.ToString() ?? "Python";
            var source = _sourceInput.Text;
            _outputBox.Text = $"Language: {language}{Environment.NewLine}" +
                              $"Source length: {source.Length}{Environment.NewLine}" +
                              "Compilation pipeline not yet wired to backend.";
        }
    }
}
