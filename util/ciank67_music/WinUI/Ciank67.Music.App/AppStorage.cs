// SPDX-License-Identifier: GPL-2.0-or-later
namespace Ciank67.Music.App;

internal static class AppStorage
{
    public static readonly string DirectoryPath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"Qmknrf5");
    public static string FlashSettings()
    {
        string path=Path.Combine(DirectoryPath,"flashing.json");
        string legacy=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"ciank67","flashing.json");
        try {if(!File.Exists(path)&&File.Exists(legacy)){Directory.CreateDirectory(DirectoryPath);File.Copy(legacy,path,overwrite:false);}}
        catch(IOException){}catch(UnauthorizedAccessException){}
        return path;
    }
}
