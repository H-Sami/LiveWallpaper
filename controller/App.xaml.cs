using System.Windows;
using System.Windows.Media;

namespace LiveWallpaper.Controller;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        ApplySystemColors();
        SystemParameters.StaticPropertyChanged += SystemParametersChanged;
        try
        {
            var window = new MainWindow();
            MainWindow = window;
            window.Show();
        }
        catch (Exception exception)
        {
            string directory = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "LiveWallpaper");
            Directory.CreateDirectory(directory);
            File.WriteAllText(Path.Combine(directory, "controller-crash.log"), exception.ToString());
            MessageBox.Show($"LiveWallpaper could not start.\n\n{exception.Message}\n\nA diagnostic log was saved to:\n{directory}",
                "LiveWallpaper", MessageBoxButton.OK, MessageBoxImage.Error);
            Shutdown(1);
        }
    }

    protected override void OnExit(ExitEventArgs e)
    {
        SystemParameters.StaticPropertyChanged -= SystemParametersChanged;
        base.OnExit(e);
    }

    private void SystemParametersChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(SystemParameters.HighContrast)) ApplySystemColors();
    }

    private void ApplySystemColors()
    {
        if (SystemParameters.HighContrast)
        {
            SetBrush("WindowBrush", SystemColors.WindowColor);
            SetBrush("SurfaceBrush", SystemColors.WindowColor);
            SetBrush("RaisedBrush", SystemColors.ControlColor);
            SetBrush("BorderBrush", SystemColors.WindowTextColor);
            SetBrush("TextBrush", SystemColors.WindowTextColor);
            SetBrush("MutedBrush", SystemColors.GrayTextColor);
            SetBrush("AccentBrush", SystemColors.HighlightColor);
            SetBrush("AccentBrightBrush", SystemColors.HighlightTextColor);
            SetBrush("AccentTextBrush", SystemColors.HighlightTextColor);
            SetBrush("AccentHoverBrush", SystemColors.HighlightColor);
            SetBrush("AccentPressedBrush", SystemColors.HighlightColor);
            SetBrush("ChromeBrush", SystemColors.WindowColor);
            SetBrush("HoverBrush", SystemColors.ControlColor);
            SetBrush("StrongBorderBrush", SystemColors.WindowTextColor);
            SetBrush("SubtleBrush", SystemColors.GrayTextColor);
            SetBrush("SuccessBrush", SystemColors.HighlightColor);
            SetBrush("ErrorBrush", SystemColors.WindowTextColor);
            SetBrush("CloseHoverBrush", SystemColors.HighlightColor);
        }
        else
        {
            SetBrush("WindowBrush", (Color)ColorConverter.ConvertFromString("#0B0B0C"));
            SetBrush("ChromeBrush", (Color)ColorConverter.ConvertFromString("#101011"));
            SetBrush("SurfaceBrush", (Color)ColorConverter.ConvertFromString("#151516"));
            SetBrush("RaisedBrush", (Color)ColorConverter.ConvertFromString("#1D1D1F"));
            SetBrush("HoverBrush", (Color)ColorConverter.ConvertFromString("#252527"));
            SetBrush("BorderBrush", (Color)ColorConverter.ConvertFromString("#2D2D30"));
            SetBrush("StrongBorderBrush", (Color)ColorConverter.ConvertFromString("#49494E"));
            SetBrush("TextBrush", (Color)ColorConverter.ConvertFromString("#F2F2F3"));
            SetBrush("MutedBrush", (Color)ColorConverter.ConvertFromString("#A1A1A6"));
            SetBrush("SubtleBrush", (Color)ColorConverter.ConvertFromString("#737378"));
            SetBrush("AccentBrush", (Color)ColorConverter.ConvertFromString("#E5E5E7"));
            SetBrush("AccentBrightBrush", Colors.White);
            SetBrush("AccentTextBrush", (Color)ColorConverter.ConvertFromString("#111112"));
            SetBrush("AccentHoverBrush", Colors.White);
            SetBrush("AccentPressedBrush", (Color)ColorConverter.ConvertFromString("#CCCCCF"));
            SetBrush("SuccessBrush", (Color)ColorConverter.ConvertFromString("#79A47C"));
            SetBrush("ErrorBrush", (Color)ColorConverter.ConvertFromString("#C98282"));
            SetBrush("CloseHoverBrush", (Color)ColorConverter.ConvertFromString("#C42B1C"));
        }
    }

    private void SetBrush(string key, Color color)
    {
        if (Resources[key] is SolidColorBrush brush && !brush.IsFrozen) brush.Color = color;
    }
}
