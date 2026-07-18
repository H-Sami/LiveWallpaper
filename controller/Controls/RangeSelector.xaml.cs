using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Input;

namespace LiveWallpaper.Controller.Controls;

public partial class RangeSelector : UserControl
{
    private double _maximum = 1;
    private double _lower;
    private double _upper = 1;

    public RangeSelector()
    {
        InitializeComponent();
        StartThumb.ValueRequested += (_, value) => SetValues(value, _upper);
        EndThumb.ValueRequested += (_, value) => SetValues(_lower, value);
        Loaded += (_, _) => UpdateVisuals();
        SizeChanged += (_, _) => UpdateVisuals();
    }

    public event EventHandler? RangeChanged;

    public double Minimum { get; private set; }
    public double Maximum => _maximum;
    public double LowerValue => _lower;
    public double UpperValue => _upper;

    public void Configure(double minimum, double maximum, double lower, double upper)
    {
        Minimum = minimum;
        _maximum = Math.Max(minimum + 0.001, maximum);
        _lower = Minimum;
        _upper = _maximum;
        SetValues(lower, upper, false);
    }

    public void SetValues(double lower, double upper, bool notify = true)
    {
        double minimumGap = Math.Min(0.25, Span);
        double newLower = Math.Clamp(lower, Minimum, _maximum - minimumGap);
        double newUpper = Math.Clamp(upper, newLower + minimumGap, _maximum);
        bool changed = Math.Abs(newLower - _lower) > 0.0001 || Math.Abs(newUpper - _upper) > 0.0001;
        _lower = newLower;
        _upper = newUpper;
        UpdateVisuals();
        if (changed && notify) RangeChanged?.Invoke(this, EventArgs.Empty);
    }

    private double UsableWidth => Math.Max(1, Surface.ActualWidth - StartThumb.Width);
    private double Span => Math.Max(0.001, _maximum - Minimum);
    private double ValueToPosition(double value) => (value - Minimum) / Span * UsableWidth;
    private double PositionToValue(double position) => Minimum + Math.Clamp(position, 0, UsableWidth) / UsableWidth * Span;

    private void UpdateVisuals()
    {
        if (!IsLoaded || Surface.ActualWidth <= 0) return;
        BaseTrack.Width = Surface.ActualWidth;
        double start = ValueToPosition(_lower);
        double end = ValueToPosition(_upper);
        Canvas.SetLeft(StartThumb, start);
        Canvas.SetLeft(EndThumb, end);
        Canvas.SetLeft(SelectedTrack, start + StartThumb.Width / 2);
        SelectedTrack.Width = Math.Max(0, end - start);
        AutomationProperties.SetName(StartThumb, $"Segment start {FormatAccessibleTime(_lower)}");
        AutomationProperties.SetName(EndThumb, $"Segment end {FormatAccessibleTime(_upper)}");
        StartThumb.Minimum = Minimum;
        StartThumb.Maximum = Math.Max(Minimum, _upper - Math.Min(0.25, Span));
        StartThumb.Value = _lower;
        EndThumb.Minimum = Math.Min(_maximum, _lower + Math.Min(0.25, Span));
        EndThumb.Maximum = _maximum;
        EndThumb.Value = _upper;
    }

    private static string FormatAccessibleTime(double seconds) =>
        TimeSpan.FromSeconds(Math.Max(0, seconds)).ToString(@"mm\:ss\.fff");

    private void StartThumb_DragDelta(object sender, System.Windows.Controls.Primitives.DragDeltaEventArgs e)
    {
        double value = PositionToValue(Canvas.GetLeft(StartThumb) + e.HorizontalChange);
        SetValues(Math.Min(value, _upper), _upper);
    }

    private void EndThumb_DragDelta(object sender, System.Windows.Controls.Primitives.DragDeltaEventArgs e)
    {
        double value = PositionToValue(Canvas.GetLeft(EndThumb) + e.HorizontalChange);
        SetValues(_lower, Math.Max(value, _lower));
    }

    private void Surface_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (e.OriginalSource is System.Windows.Controls.Primitives.Thumb) return;
        double value = PositionToValue(e.GetPosition(Surface).X - StartThumb.Width / 2);
        if (Math.Abs(value - _lower) <= Math.Abs(value - _upper))
        {
            SetValues(value, _upper);
            StartThumb.Focus();
        }
        else
        {
            SetValues(_lower, value);
            EndThumb.Focus();
        }
        e.Handled = true;
    }

    private double KeyboardStep() => Math.Max(0.1, Span / 100.0);

    private void StartThumb_KeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key is Key.Left or Key.Down) SetValues(_lower - KeyboardStep(), _upper);
        else if (e.Key is Key.Right or Key.Up) SetValues(_lower + KeyboardStep(), _upper);
        else if (e.Key == Key.PageDown) SetValues(_lower - KeyboardStep() * 10, _upper);
        else if (e.Key == Key.PageUp) SetValues(_lower + KeyboardStep() * 10, _upper);
        else if (e.Key == Key.Home) SetValues(Minimum, _upper);
        else if (e.Key == Key.End) SetValues(_upper, _upper);
        else return;
        e.Handled = true;
    }

    private void EndThumb_KeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key is Key.Left or Key.Down) SetValues(_lower, _upper - KeyboardStep());
        else if (e.Key is Key.Right or Key.Up) SetValues(_lower, _upper + KeyboardStep());
        else if (e.Key == Key.PageDown) SetValues(_lower, _upper - KeyboardStep() * 10);
        else if (e.Key == Key.PageUp) SetValues(_lower, _upper + KeyboardStep() * 10);
        else if (e.Key == Key.Home) SetValues(_lower, _lower);
        else if (e.Key == Key.End) SetValues(_lower, Maximum);
        else return;
        e.Handled = true;
    }
}
