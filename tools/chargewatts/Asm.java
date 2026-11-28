import brut.androlib.smali.SmaliBuilder;
import com.android.tools.smali.dexlib2.Opcodes;
import com.android.tools.smali.dexlib2.writer.builder.DexBuilder;
import com.android.tools.smali.dexlib2.writer.io.FileDataStore;
import java.io.File;
import java.nio.file.*;
import java.util.*;
import java.util.stream.*;

public class Asm {
  public static void main(String[] a) throws Exception {
    int api = Integer.parseInt(a[2]);
    DexBuilder db = new DexBuilder(new Opcodes(api));
    SmaliBuilder sb = new SmaliBuilder(api);
    List<Path> files;
    try (Stream<Path> s = Files.walk(Paths.get(a[0]))) { files = s.filter(p -> p.toString().endsWith(".smali")).sorted().collect(Collectors.toList()); }
    int n = 0;
    for (Path p : files) { if (!sb.buildFile(p.toFile(), db)) throw new RuntimeException("failed: " + p); n++; }
    db.writeTo(new FileDataStore(new File(a[1])));
    System.out.println("assembled " + n + " classes");
  }
}
