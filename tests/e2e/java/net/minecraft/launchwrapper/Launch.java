package net.minecraft.launchwrapper;

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.Writer;

/**
 * Заглушка net.minecraft.launchwrapper.Launch для сквозного теста лаунчера.
 * Вместо игры записывает в launch_report.txt, с какими параметрами её запустили.
 */
public class Launch {
    public static void main(String[] args) throws Exception {
        StringBuilder sb = new StringBuilder();
        sb.append("java.version=").append(System.getProperty("java.version")).append('\n');
        sb.append("java.home=").append(System.getProperty("java.home")).append('\n');
        sb.append("cwd=").append(new File(".").getCanonicalPath()).append('\n');
        sb.append("classpath=").append(System.getProperty("java.class.path")).append('\n');
        String natives = System.getProperty("java.library.path");
        sb.append("natives=").append(natives).append('\n');
        String[] list = new File(natives).list();
        if (list != null) { java.util.Arrays.sort(list); for (String n : list) sb.append("native-file=").append(n).append('\n'); }
        sb.append("maxMemoryMb=").append(Runtime.getRuntime().maxMemory() / 1048576).append('\n');
        sb.append("gdz.test=").append(System.getProperty("gdz.test")).append('\n');
        sb.append("fml.ignoreInvalidMinecraftCertificates=").append(System.getProperty("fml.ignoreInvalidMinecraftCertificates")).append('\n');
        for (String a : args) sb.append("arg=").append(a).append('\n');
        Writer w = new OutputStreamWriter(new FileOutputStream("launch_report.txt"), "UTF-8");
        w.write(sb.toString());
        w.close();
        System.out.println("fake minecraft started");
    }
}
