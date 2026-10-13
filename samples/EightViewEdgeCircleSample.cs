using System;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;

[StructLayout(LayoutKind.Sequential, Pack = 8)]
public struct WaferEightViewEdgeCircleResult
{
    public double CenterXmm;
    public double CenterYmm;
    public double RadiusMm;
    public double RmsResidualUm;
    public double MaxResidualUm;
    public double NotchAngleDeg;
}

public static class WaferEightViewEdgeCircle
{
    [DllImport("WaferCalibSDK.dll", EntryPoint = "Wafer_FindEightViewEdgeCircle",
        ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)]
    private static extern int Find(
        [In, MarshalAs(UnmanagedType.LPArray, SizeConst = 8)] IntPtr[] imageBuffers,
        [In, MarshalAs(UnmanagedType.LPArray, SizeConst = 16)] double[] stageXYmm,
        int width, int height, double pixelSizeUm, out WaferEightViewEdgeCircleResult result);

    public static WaferEightViewEdgeCircleResult Calculate(
        byte[][] images, double[] stageXYmm, int width, int height, double pixelSizeUm)
    {
        if (images == null || images.Length != 8 || stageXYmm == null || stageXYmm.Length != 16)
            throw new ArgumentException("必须输入8张图像及16个XY坐标。");
        if (width < 512 || height < 512 || !Finite(pixelSizeUm) || pixelSizeUm <= 0)
            throw new ArgumentException("宽至少512、高至少512，像素当量必须是有限正数。");
        int bytes = checked(width * height);
        for (int i = 0; i < 8; ++i)
            if (images[i] == null || images[i].Length != bytes)
                throw new ArgumentException("图像必须是width*height字节的紧凑Mono8。");
        foreach (double coordinate in stageXYmm)
            if (!Finite(coordinate)) throw new ArgumentException("轴坐标必须为有限数值。");

        var handles = new GCHandle[8];
        var pointers = new IntPtr[8];
        try
        {
            for (int i = 0; i < 8; ++i)
            {
                handles[i] = GCHandle.Alloc(images[i], GCHandleType.Pinned);
                pointers[i] = handles[i].AddrOfPinnedObject();
            }
            WaferEightViewEdgeCircleResult result;
            int code = Find(pointers, stageXYmm, width, height, pixelSizeUm, out result);
            if (code != 0) throw new InvalidOperationException("圆心计算失败，错误码：" + code);
            // 圆心成功不保证角度有效；业务使用角度前需检查NaN。
            return result;
        }
        finally
        {
            for (int i = 0; i < 8; ++i)
                if (handles[i].IsAllocated) handles[i].Free();
        }
    }

    private static bool Finite(double x) { return !double.IsNaN(x) && !double.IsInfinity(x); }
}

// 可运行的.NET Framework x64示例；业务调用只需复用上面的结构和Calculate。
internal static class EightViewEdgeCircleSample
{
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetDllDirectory(string path);

    private static byte[] ReadMono8(string path, out int width, out int height)
    {
        using (var bitmap = new Bitmap(path))
        {
            if (bitmap.PixelFormat != PixelFormat.Format8bppIndexed)
                throw new ArgumentException("此读图示例要求8位灰度图：" + path);
            width = bitmap.Width; height = bitmap.Height;
            var pixels = new byte[checked(width * height)];
            var palette = bitmap.Palette.Entries;
            for (int i = 0; i < palette.Length; ++i)
                if (palette[i].R != i || palette[i].G != i || palette[i].B != i)
                    throw new ArgumentException("此读图示例要求标准灰度调色板。");
            BitmapData data = bitmap.LockBits(new Rectangle(0, 0, width, height),
                ImageLockMode.ReadOnly, PixelFormat.Format8bppIndexed);
            try
            {
                for (int y = 0; y < height; ++y)
                    Marshal.Copy(IntPtr.Add(data.Scan0, checked(y * data.Stride)), pixels, y * width, width);
            }
            finally { bitmap.UnlockBits(data); }
            return pixels;
        }
    }

    public static int Main(string[] args)
    {
        try
        {
            if (args.Length != 3)
                throw new ArgumentException("参数：DLL目录 图片根目录 输出CSV路径");
            if (!SetDllDirectory(Path.GetFullPath(args[0])))
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            if (Marshal.SizeOf(typeof(WaferEightViewEdgeCircleResult)) != 48)
                throw new InvalidOperationException("结果结构体大小不符合C ABI。");
            var lines = new string[3];
            lines[0] = "group,center_x_mm,center_y_mm,radius_mm,rms_residual_um,max_residual_um,notch_angle_deg,call_ms";
            string[] names = { "EdgeImage_1.5X_thick_8", "EdgeImage_2.5X_thick_8" };
            for (int group = 0; group < names.Length; ++group)
            {
                string[] files = Directory.GetFiles(Path.Combine(args[1], names[group]), "*.jpg");
                Array.Sort(files, StringComparer.Ordinal);
                if (files.Length != 8) throw new ArgumentException("每组必须有8张图像。");
                var images = new byte[8][];
                var xy = new double[16];
                int width = 0, height = 0;
                for (int i = 0; i < 8; ++i)
                {
                    int w, h;
                    images[i] = ReadMono8(files[i], out w, out h);
                    if (i != 0 && (w != width || h != height))
                        throw new ArgumentException("八张图像尺寸不一致。");
                    width = w; height = h;
                    string[] tokens = Path.GetFileNameWithoutExtension(files[i]).Split('_');
                    if (tokens.Length != 3 || tokens[0] != i.ToString(CultureInfo.InvariantCulture))
                        throw new ArgumentException("文件顺序必须是0_X_Y至7_X_Y。");
                    xy[2*i] = double.Parse(tokens[1], CultureInfo.InvariantCulture);
                    xy[2*i+1] = double.Parse(tokens[2].Split(' ')[0], CultureInfo.InvariantCulture);
                }
                var clock = Stopwatch.StartNew();
                WaferEightViewEdgeCircleResult result = WaferEightViewEdgeCircle.Calculate(images, xy, width, height, group == 0 ? 3.0 : 1.8);
                clock.Stop();
                lines[group+1] = string.Join(",", new string[] {
                    names[group], result.CenterXmm.ToString("R", CultureInfo.InvariantCulture),
                    result.CenterYmm.ToString("R", CultureInfo.InvariantCulture),
                    result.RadiusMm.ToString("R", CultureInfo.InvariantCulture),
                    result.RmsResidualUm.ToString("R", CultureInfo.InvariantCulture),
                    result.MaxResidualUm.ToString("R", CultureInfo.InvariantCulture),
                    result.NotchAngleDeg.ToString("R", CultureInfo.InvariantCulture),
                    clock.Elapsed.TotalMilliseconds.ToString("F3", CultureInfo.InvariantCulture) });
                Console.WriteLine(lines[group+1]);
            }
            File.WriteAllLines(args[2], lines);
            return 0;
        }
        catch (Exception e) { Console.Error.WriteLine(e); return 1; }
    }
}
