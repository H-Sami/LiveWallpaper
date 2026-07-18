using System.Windows;
using System.Windows.Automation;
using System.Windows.Automation.Peers;
using System.Windows.Automation.Provider;
using System.Windows.Controls.Primitives;

namespace LiveWallpaper.Controller.Controls;

public sealed class RangeThumb : Thumb
{
    public static readonly DependencyProperty MinimumProperty = DependencyProperty.Register(
        nameof(Minimum), typeof(double), typeof(RangeThumb), new PropertyMetadata(0.0));
    public static readonly DependencyProperty MaximumProperty = DependencyProperty.Register(
        nameof(Maximum), typeof(double), typeof(RangeThumb), new PropertyMetadata(1.0));
    public static readonly DependencyProperty ValueProperty = DependencyProperty.Register(
        nameof(Value), typeof(double), typeof(RangeThumb),
        new PropertyMetadata(0.0, OnValueChanged));

    public double Minimum
    {
        get => (double)GetValue(MinimumProperty);
        set => SetValue(MinimumProperty, value);
    }

    public double Maximum
    {
        get => (double)GetValue(MaximumProperty);
        set => SetValue(MaximumProperty, value);
    }

    public double Value
    {
        get => (double)GetValue(ValueProperty);
        set => SetValue(ValueProperty, value);
    }

    public event EventHandler<double>? ValueRequested;

    internal void RequestValue(double value) =>
        ValueRequested?.Invoke(this, Math.Clamp(value, Minimum, Maximum));

    protected override AutomationPeer OnCreateAutomationPeer() => new RangeThumbAutomationPeer(this);

    private static void OnValueChanged(DependencyObject source, DependencyPropertyChangedEventArgs args)
    {
        if (UIElementAutomationPeer.FromElement((RangeThumb)source) is RangeThumbAutomationPeer peer)
        {
            peer.RaiseValueChanged((double)args.OldValue, (double)args.NewValue);
        }
    }
}

internal sealed class RangeThumbAutomationPeer(RangeThumb owner)
    : ThumbAutomationPeer(owner), IRangeValueProvider
{
    private RangeThumb RangeOwner => (RangeThumb)base.Owner;

    public override object? GetPattern(PatternInterface patternInterface) =>
        patternInterface == PatternInterface.RangeValue ? this : base.GetPattern(patternInterface);

    protected override string GetClassNameCore() => "RangeThumb";
    protected override AutomationControlType GetAutomationControlTypeCore() => AutomationControlType.Slider;

    public bool IsReadOnly => !RangeOwner.IsEnabled;
    public double LargeChange => Math.Max(0.1, (RangeOwner.Maximum - RangeOwner.Minimum) / 10.0);
    public double SmallChange => Math.Max(0.1, (RangeOwner.Maximum - RangeOwner.Minimum) / 100.0);
    public double Maximum => RangeOwner.Maximum;
    public double Minimum => RangeOwner.Minimum;
    public double Value => RangeOwner.Value;

    public void SetValue(double value)
    {
        if (IsReadOnly) throw new ElementNotEnabledException();
        if (value < Minimum || value > Maximum) throw new ArgumentOutOfRangeException(nameof(value));
        RangeOwner.Dispatcher.Invoke(() => RangeOwner.RequestValue(value));
    }

    internal void RaiseValueChanged(double oldValue, double newValue) =>
        RaisePropertyChangedEvent(RangeValuePatternIdentifiers.ValueProperty, oldValue, newValue);
}
