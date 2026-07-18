using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Automation.Peers;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Threading;
using LiveWallpaper.Controller.Models;
using LiveWallpaper.Controller.Services;
using Microsoft.Win32;

namespace LiveWallpaper.Controller;

public partial class MainWindow : Window
{
    private readonly DispatcherTimer _previewTimer = new() { Interval = TimeSpan.FromMilliseconds(33) };
    private readonly DispatcherTimer _toastTimer = new() { Interval = TimeSpan.FromSeconds(3) };
    private readonly DispatcherTimer _hostTimer = new() { Interval = TimeSpan.FromSeconds(1) };
    private readonly DispatcherTimer _previewLoadTimer = new() { Interval = TimeSpan.FromSeconds(10) };
    private WallpaperItem? _selected;
    private double _durationSeconds;
    private double _pendingStartSeconds;
    private double _pendingEndSeconds;
    private bool _previewPlaying = true;
    private bool _updatingControls;
    private bool _initializingSettings = true;
    private bool _previewUnavailable;
    private string _currentPage = "library";
    private readonly ControllerUiState? _restoredUiState;

    public ObservableCollection<WallpaperItem> LibraryItems { get; } = [];

    public MainWindow()
    {
        InitializeComponent();
        StateChanged += (_, _) => UpdateCaptionState();
        DataContext = this;
        _restoredUiState = UiStateStore.Load();
        RestoreWindowBounds();

        foreach (WallpaperItem item in LibraryStore.Load().Where(item => SupportedMedia.IsSupportedPath(item.Path)))
        {
            LibraryItems.Add(item);
        }
        WallpaperSettings? current = SettingsStore.Load();
        if (current?.Playing == true && HostController.IsRunning)
        {
            WallpaperItem? playing = LibraryItems.FirstOrDefault(item =>
                string.Equals(item.Path, current.MediaPath, StringComparison.OrdinalIgnoreCase));
            if (playing is not null) playing.IsPlaying = true;
        }

        SegmentSelector.RangeChanged += (_, _) => SegmentRangeChanged();
        _previewTimer.Tick += (_, _) => UpdatePreviewProgress();
        _toastTimer.Tick += (_, _) => { _toastTimer.Stop(); Toast.Visibility = Visibility.Collapsed; };
        _hostTimer.Tick += (_, _) => UpdateHostState();
        _previewLoadTimer.Tick += (_, _) => SetPreviewUnavailable(
            "Preview timed out. You can still apply the full video using the native host.");
        _hostTimer.Start();

        SourceInitialized += (_, _) => EnableImmersiveDarkMode();
        SizeChanged += (_, _) => UpdateResponsiveLayout();
        Closing += SaveUiState;
        Closed += (_, _) => ClosePreview();
        Loaded += (_, _) =>
        {
            RestoreWorkflowState();
            UpdateCaptionState();
        };

        DataPathText.Text = SettingsStore.DataDirectory;
        _initializingSettings = true;
        StartWithWindowsSwitch.IsChecked = HostController.StartsWithWindows;
        _initializingSettings = false;
        RefreshLibraryState();
        UpdateHostState();
    }

    private void ShowPage(UIElement page)
    {
        LibraryPage.Visibility = page == LibraryPage ? Visibility.Visible : Visibility.Collapsed;
        EditorPage.Visibility = page == EditorPage ? Visibility.Visible : Visibility.Collapsed;
        SettingsPage.Visibility = page == SettingsPage ? Visibility.Visible : Visibility.Collapsed;
        SetNavState(LibraryNav, page == LibraryPage);
        SetNavState(EditorNav, page == EditorPage);
        SetNavState(SettingsNav, page == SettingsPage);
        _currentPage = page == EditorPage ? "editor" : page == SettingsPage ? "settings" : "library";
    }

    private static void SetNavState(Button button, bool selected)
    {
        bool highContrast = SystemParameters.HighContrast;
        button.Background = new SolidColorBrush(highContrast
            ? (selected ? SystemColors.HighlightColor : SystemColors.WindowColor)
            : (Color)ColorConverter.ConvertFromString(selected ? "#252527" : "#00000000"));
        button.Foreground = new SolidColorBrush(highContrast
            ? (selected ? SystemColors.HighlightTextColor : SystemColors.WindowTextColor)
            : (Color)ColorConverter.ConvertFromString(selected ? "#F2F2F3" : "#A1A1A6"));
        button.BorderBrush = new SolidColorBrush(highContrast
            ? SystemColors.WindowTextColor
            : (Color)ColorConverter.ConvertFromString(selected ? "#49494E" : "#00000000"));
        AutomationProperties.SetItemStatus(button, selected ? "Current page" : string.Empty);
    }

    private void UpdateResponsiveLayout()
    {
        bool compact = ActualWidth < 800;
        SidebarColumn.Width = new GridLength(compact ? 68 : 204);
        WorkspaceLabel.Visibility = compact ? Visibility.Collapsed : Visibility.Visible;
        LibraryNavLabel.Visibility = compact ? Visibility.Collapsed : Visibility.Visible;
        EditorNavLabel.Visibility = compact ? Visibility.Collapsed : Visibility.Visible;
        SettingsNavLabel.Visibility = compact ? Visibility.Collapsed : Visibility.Visible;
        HostStatusCard.Visibility = compact ? Visibility.Collapsed : Visibility.Visible;
        LibraryNav.HorizontalContentAlignment = compact ? HorizontalAlignment.Center : HorizontalAlignment.Left;
        EditorNav.HorizontalContentAlignment = compact ? HorizontalAlignment.Center : HorizontalAlignment.Left;
        SettingsNav.HorizontalContentAlignment = compact ? HorizontalAlignment.Center : HorizontalAlignment.Left;
        LibraryPage.Margin = compact ? new Thickness(20, 20, 20, 22) : new Thickness(34, 27, 34, 30);
        EditorPage.Margin = compact ? new Thickness(18, 18, 18, 20) : new Thickness(32, 24, 32, 28);
        EditorPreviewColumn.Width = new GridLength(compact ? 1.25 : 1.7, GridUnitType.Star);
        EditorGapColumn.Width = new GridLength(compact ? 8 : 16);
        EditorOptionsColumn.Width = new GridLength(compact ? 1.0 : 0.92, GridUnitType.Star);
    }

    private void RestoreWindowBounds()
    {
        if (_restoredUiState is null) return;
        double width = Math.Clamp(_restoredUiState.Width, MinWidth, Math.Max(MinWidth, SystemParameters.VirtualScreenWidth));
        double height = Math.Clamp(_restoredUiState.Height, MinHeight, Math.Max(MinHeight, SystemParameters.VirtualScreenHeight));
        double left = Math.Clamp(_restoredUiState.Left, SystemParameters.VirtualScreenLeft,
            SystemParameters.VirtualScreenLeft + Math.Max(0, SystemParameters.VirtualScreenWidth - width));
        double top = Math.Clamp(_restoredUiState.Top, SystemParameters.VirtualScreenTop,
            SystemParameters.VirtualScreenTop + Math.Max(0, SystemParameters.VirtualScreenHeight - height));
        WindowStartupLocation = WindowStartupLocation.Manual;
        Width = width;
        Height = height;
        Left = left;
        Top = top;
        if (_restoredUiState.Maximized) WindowState = WindowState.Maximized;
    }

    private void RestoreWorkflowState()
    {
        SearchBox.Text = _restoredUiState?.SearchText ?? string.Empty;
        WallpaperItem? restoredItem = _restoredUiState?.SelectedPath is string path
            ? LibraryItems.FirstOrDefault(item => string.Equals(item.Path, path, StringComparison.OrdinalIgnoreCase))
            : null;
        if (_restoredUiState?.Page == "editor" && restoredItem?.Exists == true)
            SelectWallpaper(restoredItem);
        else if (_restoredUiState?.Page == "settings")
            ShowPage(SettingsPage);
        else
            ShowPage(LibraryPage);
        UpdateResponsiveLayout();
    }

    private void SaveUiState(object? sender, CancelEventArgs e)
    {
        try
        {
            Rect bounds = RestoreBounds;
            UiStateStore.Save(new ControllerUiState(
                bounds.Left, bounds.Top, bounds.Width, bounds.Height,
                WindowState == WindowState.Maximized, _currentPage, _selected?.Path, SearchBox.Text));
        }
        catch
        {
            // UI state is optional and must never prevent controller shutdown.
        }
    }

    private void LibraryNav_Click(object sender, RoutedEventArgs e) => ShowPage(LibraryPage);
    private void EditorNav_Click(object sender, RoutedEventArgs e)
    {
        if (_selected is null)
        {
            if (LibraryItems.Count > 0) SelectWallpaper(LibraryItems[0]);
            else
            {
                ShowPage(LibraryPage);
                ShowToast("Add an MP4, WebM, MKV, or MOV video before opening the editor.", false);
                return;
            }
        }
        ShowPage(EditorPage);
    }
    private void SettingsNav_Click(object sender, RoutedEventArgs e) => ShowPage(SettingsPage);
    private void BackToLibrary_Click(object sender, RoutedEventArgs e) => ShowPage(LibraryPage);

    private void AddWallpaper_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog
        {
            Title = "Add live wallpapers",
            Filter = SupportedMedia.OpenFileFilter,
            Multiselect = true,
            CheckFileExists = true
        };
        if (dialog.ShowDialog(this) != true) return;
        WallpaperItem? last = null;
        foreach (string file in dialog.FileNames) last = ImportWallpaper(file);
        if (last is not null) SelectWallpaper(last);
    }

    private WallpaperItem? ImportWallpaper(string file)
    {
        string extension = Path.GetExtension(file);
        if (!SupportedMedia.IsSupportedExtension(extension))
        {
            ShowToast("Only MP4, WebM, MKV, and MOV video wallpapers are supported.", false);
            return null;
        }
        string fullPath = Path.GetFullPath(file);
        WallpaperItem? existing = LibraryItems.FirstOrDefault(item =>
            string.Equals(item.Path, fullPath, StringComparison.OrdinalIgnoreCase));
        if (existing is not null)
        {
            ShowToast("That wallpaper is already in your library.");
            return existing;
        }
        var item = new WallpaperItem { Path = fullPath };
        LibraryItems.Insert(0, item);
        LibraryStore.Save(LibraryItems);
        RefreshLibraryState();
        ShowToast($"Added {item.FileName}");
        return item;
    }

    private void WallpaperCard_Click(object sender, RoutedEventArgs e)
    {
        if (sender is Button { Tag: WallpaperItem item }) SelectWallpaper(item);
    }

    private void SelectWallpaper(WallpaperItem item)
    {
        if (!item.Exists)
        {
            ShowToast("This source file is missing. Remove it or restore the file.", false);
            return;
        }
        foreach (WallpaperItem candidate in LibraryItems) candidate.IsSelected = candidate == item;
        _selected = item;
        EditorTitle.Text = item.Name;
        EditorSubtitle.Text = item.Meta;
        FilePathText.Text = item.Path;
        AudioSwitch.IsEnabled = item.IsVideo;
        VolumeSlider.IsEnabled = item.IsVideo;
        SegmentSelector.IsEnabled = item.IsVideo;
        StartTimeBox.IsEnabled = item.IsVideo;
        EndTimeBox.IsEnabled = item.IsVideo;
        WallpaperSettings? saved = SettingsStore.Load();
        bool matchesSaved = saved is not null &&
            string.Equals(saved.MediaPath, item.Path, StringComparison.OrdinalIgnoreCase);
        AudioSwitch.IsChecked = item.IsVideo && matchesSaved && !saved!.Muted;
        VolumeSlider.Value = matchesSaved ? saved!.Volume : 50;
        _pendingStartSeconds = matchesSaved ? saved!.StartMs / 1000.0 : 0;
        _pendingEndSeconds = matchesSaved ? saved!.EndMs / 1000.0 : 0;
        LoadPreview(item);
        ShowPage(EditorPage);
    }

    private void LoadPreview(WallpaperItem item)
    {
        ClosePreview();
        _previewUnavailable = false;
        _previewLoadTimer.Start();
        _previewPlaying = true;
        PreviewPlayButton.Content = "Pause";
        PreviewPlaceholder.Visibility = Visibility.Collapsed;
        _durationSeconds = 0;
        _updatingControls = true;
        SegmentSelector.Configure(0, 1, 0, 1);
        StartTimeBox.Text = "00:00.000";
        EndTimeBox.Text = "Loading…";
        SegmentLengthText.Text = "Reading duration";
        PreviewPosition.Value = 0;
        PreviewPosition.Maximum = 1;
        PreviewPlayButton.IsEnabled = false;
        PreviewPosition.IsEnabled = false;
        _updatingControls = false;

        try
        {
            VideoPreview.Visibility = Visibility.Visible;
            VideoPreview.Source = new Uri(item.Path);
            VideoPreview.Volume = VolumeSlider.Value / 100.0;
            VideoPreview.IsMuted = AudioSwitch.IsChecked != true;
            // LoadedBehavior is Manual, so setting Source alone does not
            // start graph construction or raise MediaOpened.
            VideoPreview.Play();
        }
        catch (Exception exception)
        {
            SetPreviewUnavailable($"Preview failed: {exception.Message}. You can still apply the full video.");
        }
    }

    private void ClosePreview()
    {
        _previewTimer.Stop();
        _previewLoadTimer.Stop();
        try
        {
            VideoPreview.Stop();
            VideoPreview.Close();
            VideoPreview.Source = null;
        }
        catch { }
    }

    private void VideoPreview_MediaOpened(object sender, RoutedEventArgs e)
    {
        if (!VideoPreview.NaturalDuration.HasTimeSpan) return;
        _previewLoadTimer.Stop();
        _previewUnavailable = false;
        PreviewPlayButton.IsEnabled = true;
        PreviewPosition.IsEnabled = true;
        SegmentSelector.IsEnabled = true;
        StartTimeBox.IsEnabled = true;
        EndTimeBox.IsEnabled = true;
        _durationSeconds = Math.Max(0.001, VideoPreview.NaturalDuration.TimeSpan.TotalSeconds);
        _updatingControls = true;
        double start = Math.Clamp(_pendingStartSeconds, 0, _durationSeconds);
        double end = _pendingEndSeconds > start
            ? Math.Clamp(_pendingEndSeconds, start, _durationSeconds)
            : _durationSeconds;
        if (end <= start) { start = 0; end = _durationSeconds; }
        SegmentSelector.Configure(0, _durationSeconds, start, end);
        PreviewPosition.Minimum = 0;
        PreviewPosition.Maximum = _durationSeconds;
        StartTimeBox.Text = FormatTime(start);
        EndTimeBox.Text = FormatTime(end);
        _updatingControls = false;
        UpdateSegmentLabels();
        VideoPreview.Position = TimeSpan.FromSeconds(start);
        VideoPreview.Play();
        _previewTimer.Start();
    }

    private void VideoPreview_MediaFailed(object sender, ExceptionRoutedEventArgs e)
    {
        SetPreviewUnavailable(
            $"Windows could not preview this video: {e.ErrorException.Message}. You can still apply the full video.");
    }

    private void SetPreviewUnavailable(string message)
    {
        _previewLoadTimer.Stop();
        _previewTimer.Stop();
        _previewUnavailable = true;
        _previewPlaying = false;
        PreviewPlaceholder.Visibility = Visibility.Visible;
        PreviewPlayButton.Content = "Preview unavailable";
        PreviewPlayButton.IsEnabled = false;
        PreviewPosition.IsEnabled = false;
        SegmentSelector.IsEnabled = false;
        StartTimeBox.IsEnabled = false;
        EndTimeBox.IsEnabled = false;
        SegmentLengthText.Text = "Full video will be applied";
        EndTimeBox.Text = "Full video";
        ShowToast(message, false);
    }

    private void UpdatePreviewProgress()
    {
        if (_selected?.IsVideo != true || _durationSeconds <= 0) return;
        double position = VideoPreview.Position.TotalSeconds;
        if (_previewPlaying && position >= SegmentSelector.UpperValue - 0.03)
        {
            position = SegmentSelector.LowerValue;
            VideoPreview.Position = TimeSpan.FromSeconds(position);
            VideoPreview.Play();
        }
        _updatingControls = true;
        PreviewPosition.Value = Math.Clamp(position, 0, _durationSeconds);
        _updatingControls = false;
        PreviewTimeText.Text = $"{FormatShort(position)} / {FormatShort(_durationSeconds)}";
    }

    private void PreviewPlayButton_Click(object sender, RoutedEventArgs e)
    {
        if (_selected is null) return;
        _previewPlaying = !_previewPlaying;
        PreviewPlayButton.Content = _previewPlaying ? "Pause" : "Play";
        if (_previewPlaying)
        {
            VideoPreview.Play();
            _previewTimer.Start();
        }
        else
        {
            VideoPreview.Pause();
        }
    }

    private void PreviewPosition_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (_updatingControls || _selected?.IsVideo != true || _durationSeconds <= 0) return;
        VideoPreview.Position = TimeSpan.FromSeconds(Math.Clamp(e.NewValue, 0, _durationSeconds));
        PreviewTimeText.Text = $"{FormatShort(e.NewValue)} / {FormatShort(_durationSeconds)}";
    }

    private void SegmentRangeChanged()
    {
        if (_updatingControls || _selected?.IsVideo != true) return;
        _updatingControls = true;
        StartTimeBox.Text = FormatTime(SegmentSelector.LowerValue);
        EndTimeBox.Text = FormatTime(SegmentSelector.UpperValue);
        if (VideoPreview.Position.TotalSeconds < SegmentSelector.LowerValue ||
            VideoPreview.Position.TotalSeconds > SegmentSelector.UpperValue)
            VideoPreview.Position = TimeSpan.FromSeconds(SegmentSelector.LowerValue);
        _updatingControls = false;
        UpdateSegmentLabels();
    }

    private void TimeBox_LostKeyboardFocus(object sender, KeyboardFocusChangedEventArgs e)
    {
        if (_updatingControls || _selected?.IsVideo != true) return;
        if (!TryParseTime(StartTimeBox.Text, out double start) ||
            !TryParseTime(EndTimeBox.Text, out double end) || start < 0 || end - start < 0.25 || end > _durationSeconds)
        {
            ShowToast("Use a valid time such as 00:12.500; the loop must be at least 250 milliseconds.", false);
            _updatingControls = true;
            StartTimeBox.Text = FormatTime(SegmentSelector.LowerValue);
            EndTimeBox.Text = FormatTime(SegmentSelector.UpperValue);
            _updatingControls = false;
            return;
        }
        SegmentSelector.SetValues(start, end);
    }

    private void AudioSwitch_Changed(object sender, RoutedEventArgs e)
    {
        if (_updatingControls || _selected?.IsVideo != true) return;
        VideoPreview.IsMuted = AudioSwitch.IsChecked != true;
    }

    private void VolumeSlider_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (VolumeText is null) return;
        VolumeText.Text = $"{Math.Round(e.NewValue):0}%";
        if (VideoPreview is not null) VideoPreview.Volume = e.NewValue / 100.0;
    }

    private void ApplyWallpaper_Click(object sender, RoutedEventArgs e)
    {
        if (_selected is null || !_selected.Exists)
        {
            ShowToast("Choose a valid wallpaper first.", false);
            return;
        }
        long startMs = 0;
        long endMs = 0;
        if (_selected.IsVideo)
        {
            if (_durationSeconds <= 0 && !_previewUnavailable)
            {
                ShowToast("Wait for the video duration before applying it.", false);
                return;
            }
            if (!_previewUnavailable)
            {
                if (SegmentSelector.UpperValue - SegmentSelector.LowerValue < 0.25)
                {
                    ShowToast("Choose a loop range of at least 250 milliseconds.", false);
                    return;
                }
                startMs = (long)Math.Round(SegmentSelector.LowerValue * 1000);
                endMs = (long)Math.Round(SegmentSelector.UpperValue * 1000);
            }
        }
        var requestedSettings = new WallpaperSettings(
            _selected.Path,
            AudioSwitch.IsChecked != true,
            (int)Math.Round(VolumeSlider.Value),
            startMs,
            endMs,
            true);
        if (!WallpaperApplication.Apply(requestedSettings, out string error))
        {
            ShowToast(error, false);
            return;
        }
        foreach (WallpaperItem item in LibraryItems) item.IsPlaying = item == _selected;
        ShowToast("Wallpaper applied. The native host will keep it running.");
        UpdateHostState();
    }

    private void StopWallpaper_Click(object sender, RoutedEventArgs e)
    {
        TryStopWallpaper();
    }

    private bool TryStopWallpaper(bool showSuccess = true)
    {
        if (!HostController.Send("--stop", out string error))
        {
            ShowToast(error, false);
            return false;
        }
        foreach (WallpaperItem item in LibraryItems) item.IsPlaying = false;
        if (showSuccess) ShowToast("Wallpaper stopped.");
        UpdateHostState();
        return true;
    }

    private void RemoveWallpaper_Click(object sender, RoutedEventArgs e)
    {
        if (_selected is null) return;
        WallpaperItem removed = _selected;
        if (removed.IsPlaying)
        {
            if (MessageBox.Show(this,
                    "This wallpaper is currently active. Stop it and remove it from the library?\n\nThe source file will not be deleted.",
                    "Remove active wallpaper", MessageBoxButton.YesNo, MessageBoxImage.Warning) != MessageBoxResult.Yes)
                return;
            if (!TryStopWallpaper(false)) return;
        }
        ClosePreview();
        LibraryItems.Remove(removed);
        _selected = null;
        LibraryStore.Save(LibraryItems);
        RefreshLibraryState();
        ShowPage(LibraryPage);
        ShowToast("Removed from the library. The source file was not deleted.");
    }

    private void SearchBox_TextChanged(object sender, TextChangedEventArgs e)
    {
        if (SearchHint is null || LibraryList is null) return;
        SearchHint.Visibility = string.IsNullOrEmpty(SearchBox.Text) ? Visibility.Visible : Visibility.Collapsed;
        var view = System.Windows.Data.CollectionViewSource.GetDefaultView(LibraryItems);
        string query = SearchBox.Text.Trim();
        view.Filter = item => item is WallpaperItem wallpaper &&
            (string.IsNullOrEmpty(query) || wallpaper.Name.Contains(query, StringComparison.OrdinalIgnoreCase));
        view.Refresh();
        RefreshLibraryState();
    }

    private void RefreshLibraryState()
    {
        if (EmptyState is null) return;
        var view = System.Windows.Data.CollectionViewSource.GetDefaultView(LibraryItems);
        int visible = view.Cast<object>().Count();
        EmptyState.Visibility = visible == 0 ? Visibility.Visible : Visibility.Collapsed;
        LibraryCountText.Text = visible == 1 ? "1 wallpaper" : $"{visible} wallpapers";
        EditorNav.IsEnabled = _selected is not null || LibraryItems.Count > 0;
    }

    private void Window_DragOver(object sender, DragEventArgs e)
    {
        e.Effects = e.Data.GetDataPresent(DataFormats.FileDrop) ? DragDropEffects.Copy : DragDropEffects.None;
        e.Handled = true;
    }

    private void Window_Drop(object sender, DragEventArgs e)
    {
        if (e.Data.GetData(DataFormats.FileDrop) is not string[] files) return;
        WallpaperItem? last = null;
        foreach (string file in files) last = ImportWallpaper(file) ?? last;
        if (last is not null) SelectWallpaper(last);
    }

    private void StartWithWindows_Changed(object sender, RoutedEventArgs e)
    {
        if (_initializingSettings) return;
        try
        {
            HostController.StartsWithWindows = StartWithWindowsSwitch.IsChecked == true;
            ShowToast(StartWithWindowsSwitch.IsChecked == true ? "Wallpaper will start with Windows." : "Windows startup disabled.");
        }
        catch (Exception exception)
        {
            _initializingSettings = true;
            StartWithWindowsSwitch.IsChecked = HostController.StartsWithWindows;
            _initializingSettings = false;
            ShowToast($"Startup setting failed: {exception.Message}", false);
        }
    }

    private void ExitHost_Click(object sender, RoutedEventArgs e)
    {
        if (!HostController.IsRunning)
        {
            ShowToast("The native host is already stopped.");
            return;
        }
        if (!HostController.Send("--exit", out string error))
        {
            ShowToast(error, false);
            return;
        }
        foreach (WallpaperItem item in LibraryItems) item.IsPlaying = false;
        ShowToast("Background host exited.");
    }

    private void OpenDataFolder_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            Directory.CreateDirectory(SettingsStore.DataDirectory);
            Process.Start(new ProcessStartInfo("explorer.exe", SettingsStore.DataDirectory) { UseShellExecute = true });
        }
        catch (Exception exception)
        {
            ShowToast($"Could not open the data folder: {exception.Message}", false);
        }
    }

    private void UpdateHostState()
    {
        bool running = HostController.IsRunning;
        HostStateText.Text = running ? "Native host running" : "Host stopped";
        HostDetailsText.Text = running ? "Running in the background with native Windows media APIs." : "Stopped. It starts automatically when a wallpaper is applied.";
        HostDot.Fill = new SolidColorBrush(SystemParameters.HighContrast
            ? (running ? SystemColors.HighlightColor : SystemColors.GrayTextColor)
            : (Color)ColorConverter.ConvertFromString(running ? "#79A47C" : "#737378"));
        AutomationProperties.SetItemStatus(HostStateText, running ? "Running" : "Stopped");
        ExitHostButton.IsEnabled = running;
    }

    private void UpdateSegmentLabels()
    {
        double length = Math.Max(0, SegmentSelector.UpperValue - SegmentSelector.LowerValue);
        SegmentLengthText.Text = length >= _durationSeconds - 0.01 ? "Full video" : $"{FormatShort(length)} loop";
    }

    private void ShowToast(string text, bool success = true)
    {
        ToastText.Text = text;
        ToastDot.Fill = new SolidColorBrush(SystemParameters.HighContrast
            ? (success ? SystemColors.HighlightColor : SystemColors.WindowTextColor)
            : (Color)ColorConverter.ConvertFromString(success ? "#79A47C" : "#C98282"));
        Toast.Visibility = Visibility.Visible;
        _toastTimer.Stop();
        if (success) _toastTimer.Start();
        (UIElementAutomationPeer.FromElement(ToastText) ??
            UIElementAutomationPeer.CreatePeerForElement(ToastText))?.RaiseAutomationEvent(
                AutomationEvents.LiveRegionChanged);
    }

    private void DismissToast_Click(object sender, RoutedEventArgs e)
    {
        _toastTimer.Stop();
        Toast.Visibility = Visibility.Collapsed;
    }

    private static string FormatTime(double seconds)
    {
        TimeSpan value = TimeSpan.FromSeconds(Math.Max(0, seconds));
        return value.TotalHours >= 1
            ? $"{(int)value.TotalHours:00}:{value.Minutes:00}:{value.Seconds:00}.{value.Milliseconds:000}"
            : $"{value.Minutes:00}:{value.Seconds:00}.{value.Milliseconds:000}";
    }

    private static string FormatShort(double seconds)
    {
        TimeSpan value = TimeSpan.FromSeconds(Math.Max(0, seconds));
        return value.TotalHours >= 1
            ? $"{(int)value.TotalHours}:{value.Minutes:00}:{value.Seconds:00}"
            : $"{(int)value.TotalMinutes:00}:{value.Seconds:00}";
    }

    private static bool TryParseTime(string text, out double seconds)
    {
        seconds = 0;
        string[] parts = text.Trim().Split(':');
        if (parts.Length is < 2 or > 3) return false;
        if (!double.TryParse(parts[^1], NumberStyles.AllowDecimalPoint, CultureInfo.InvariantCulture, out double last) || last < 0 || last >= 60)
            return false;
        if (!int.TryParse(parts[^2], out int minutes) || minutes < 0 || minutes >= (parts.Length == 3 ? 60 : int.MaxValue))
            return false;
        int hours = 0;
        if (parts.Length == 3 && (!int.TryParse(parts[0], out hours) || hours < 0)) return false;
        seconds = hours * 3600.0 + minutes * 60.0 + last;
        return true;
    }

    private void TitleBar_MouseRightButtonUp(object sender, MouseButtonEventArgs e) =>
        SystemCommands.ShowSystemMenu(this, PointToScreen(e.GetPosition(this)));

    private void MinimizeButton_Click(object sender, RoutedEventArgs e) =>
        SystemCommands.MinimizeWindow(this);

    private void MaximizeButton_Click(object sender, RoutedEventArgs e)
    {
        if (WindowState == WindowState.Maximized)
            SystemCommands.RestoreWindow(this);
        else
            SystemCommands.MaximizeWindow(this);
    }

    private void CloseButton_Click(object sender, RoutedEventArgs e) =>
        SystemCommands.CloseWindow(this);

    private void UpdateCaptionState()
    {
        if (MaximizeGlyph is null || RestoreGlyph is null) return;
        bool maximized = WindowState == WindowState.Maximized;
        MaximizeGlyph.Visibility = maximized ? Visibility.Collapsed : Visibility.Visible;
        RestoreGlyph.Visibility = maximized ? Visibility.Visible : Visibility.Collapsed;
        WindowFrame.BorderThickness = maximized ? new Thickness(0) : new Thickness(1);
        string action = maximized ? "Restore" : "Maximize";
        AutomationProperties.SetName(MaximizeButton, action);
    }

    private void EnableImmersiveDarkMode()
    {
        IntPtr handle = new WindowInteropHelper(this).Handle;
        int enabled = 1;
        DwmSetWindowAttribute(handle, 20, ref enabled, sizeof(int));
    }

    [DllImport("dwmapi.dll")]
    private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attribute, ref int value, int size);
}
