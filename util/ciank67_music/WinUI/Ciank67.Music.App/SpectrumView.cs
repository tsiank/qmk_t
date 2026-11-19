using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Media;

namespace Ciank67.Music.App;

internal sealed class SpectrumView
{
    public Grid View { get; } = new();
    private readonly Border[] bars = new Border[16];
    private byte[] levels = new byte[16];

    public SpectrumView()
    {
        View.Height = 250;
        View.ColumnSpacing = 8;
        for (int i = 0; i < bars.Length; ++i)
        {
            View.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            var cell = new Grid();
            cell.Children.Add(new Border { Background = new SolidColorBrush(Colors.Gray) { Opacity = 0.10 }, CornerRadius = new CornerRadius(5) });
            bars[i] = new Border
            {
                Background = new SolidColorBrush(ColorForBand(i)),
                CornerRadius = new CornerRadius(5), VerticalAlignment = VerticalAlignment.Bottom, Height = 0
            };
            cell.Children.Add(bars[i]);
            Grid.SetColumn(cell, i);
            View.Children.Add(cell);
        }
        View.SizeChanged += (_, _) => SetLevels(levels);
        AutomationProperties.SetName(View, "16 段实时音乐频谱，频率从左到右升高");
    }

    public void SetLevels(byte[] values)
    {
        if (values.Length != 16) return;
        levels = values;
        double height = View.ActualHeight > 0 ? View.ActualHeight : View.Height;
        for (int i = 0; i < bars.Length; ++i) bars[i].Height = height * values[i] / 255;
    }

    private static Windows.UI.Color ColorForBand(int band)
    {
        double hue = band / 15.0 * 0.75;
        double h = hue * 6;
        double x = 1 - Math.Abs(h % 2 - 1);
        (double r, double g, double b) = (int)h switch
        {
            0 => (1.0, x, 0.0), 1 => (x, 1.0, 0.0), 2 => (0.0, 1.0, x),
            3 => (0.0, x, 1.0), 4 => (x, 0.0, 1.0), _ => (1.0, 0.0, x)
        };
        return Windows.UI.Color.FromArgb(255, (byte)(r * 220), (byte)(g * 220), (byte)(b * 220));
    }
}
