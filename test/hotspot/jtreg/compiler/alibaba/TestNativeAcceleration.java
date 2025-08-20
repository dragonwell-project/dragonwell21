/*
 * Copyright (c) 2025, Alibaba Group Holding Limited. All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

/**
 * @test TestNativeAcceleration
 * @summary Check that HotSpot can replace Java methods with user-provided native functions
 * @requires vm.compiler2.enabled
 * @library /test/lib /
 * @build jdk.test.whitebox.WhiteBox
 * @run driver jdk.test.lib.helpers.ClassFileInstaller jdk.test.whitebox.WhiteBox
 * @run main/native/othervm -Xbootclasspath/a:. -XX:+UnlockDiagnosticVMOptions -XX:+WhiteBoxAPI TestNativeAcceleration
 */

import jdk.test.lib.process.OutputAnalyzer;
import jdk.test.lib.process.ProcessTools;
import jdk.test.whitebox.WhiteBox;
import compiler.whitebox.CompilerWhiteBoxTest;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

public class TestNativeAcceleration {
    // private static final String TEST_LIB = System.getProperty("test.nativepath") + "/nativeaccel_1.0.so";
    // private static final String TEST_LIB2 = System.getProperty("test.nativepath") + "/nativeaccel_2.0.so";
    private static final String TEST_LIB =  "libNativeAccel_1";
    private static final String TEST_LIB2 = "libNativeAccel_2";
    private static final WhiteBox WHITE_BOX = WhiteBox.getWhiteBox();

    public static void main(String[] args) throws Exception {
        // These should work.
        testRuleParseOk();                                      // Just `-version`.
        // testRuleParseOk("-XX:AIExtensionUnit=");             // No units.
        testRuleParseOk("-XX:AIExtensionUnit=" + TEST_LIB);  // A valid unit.
        testRuleParseOk("-XX:AIExtensionUnit=" + TEST_LIB2); // A valid unit but no finalizer.

        // Invalid acceleration library path.
        testRuleParseError("-XX:AIExtensionUnit=?");

        // Duplicate units.
        /*
        testRuleParseError("-XX:AIExtensionUnit=" + TEST_LIB,
                           "-XX:AIExtensionUnit=" + TEST_LIB);
        */

        // Duplicate entries in different units.
        /*
        testRuleParseError("-XX:AIExtensionUnit=" + TEST_LIB,
                           "-XX:AIExtensionUnit=" + TEST_LIB2);
        */

        // Some units are invalid.
        testRuleParseError("-XX:AIExtensionUnit=" + TEST_LIB,
                           "-XX:AIExtensionUnit=?");
        testRuleParseError("-XX:AIExtensionUnit=?",
                           "-XX:AIExtensionUnit=" + TEST_LIB);

        // Test method compilation.
        testMethodCompile("hello").shouldContain("Hello from");
        testMethodCompile("hello_int", "42").shouldContain("42 (int)");
        testMethodCompile("hello_long", "43").shouldContain("43 (long)");
        testMethodCompile("hello_float", "44").shouldContain("44.00 (float)");
        testMethodCompile("hello_double", "45").shouldContain("45.00 (double)");
        testMethodCompile("hello_bytes", "test method compile")
            .shouldContain("test method compile (bytes)");
        testMethodCompile("hello_object", "hi")
            .shouldContain("0x")
            .shouldContain("(object)");
        testMethodCompile("hello_short_method", "46")
            .shouldContain("0x")
            .shouldContain("(this)")
            .shouldContain("46 (short)");
        testMethodCompile("add_ints", "47", "48").shouldContain("95");
        testMethodCompile("add_doubles", "47", "48").shouldContain("95.00");
        testMethodCompile("add_arrays", "3", "1", "2", "2").shouldContain("3\n3\n1\n");
    }

    private static void testRuleParseOk(String... commands) throws Exception {
        OutputAnalyzer output = getJavaVersionOutput(commands);
        output.shouldHaveExitValue(0);
    }

    private static void testRuleParseError(String... commands) throws Exception {
        OutputAnalyzer output = getJavaVersionOutput(commands);
        output.shouldNotHaveExitValue(0);
        output.shouldContain("Invalid ai extension option");
        output.shouldContain("Could not create the Java Virtual Machine");
    }

    private static OutputAnalyzer getJavaVersionOutput(String... commands) throws Exception {
        ArrayList<String> args = new ArrayList<>(List.of(
            "-XX:+UseAIExtension",
            "-XX:+UnlockExperimentalVMOptions"
        ));
        args.addAll(List.of(commands));
        args.add("-version");

        ProcessBuilder pb = ProcessTools.createLimitedTestJavaProcessBuilder(args);
        // setup ALT_AI_EXT_PATH for testing
        pb.environment().put("ALT_AI_EXT_PATH", System.getProperty("test.nativepath"));
        return ProcessTools.executeCommand(pb);
    }

    private static OutputAnalyzer testMethodCompile(String... testArgs) throws Exception {
        ArrayList<String> args = new ArrayList<>(List.of(
            "-Xbootclasspath/a:.",
            "-XX:+UnlockDiagnosticVMOptions",
            "-XX:+WhiteBoxAPI",
            "-XX:-BackgroundCompilation",
            // "-XX:CompileCommand=print,TestNativeAcceleration$Launcher::dispatch", // For debugging.
            "-XX:+UseAIExtension",
            "-XX:+UnlockExperimentalVMOptions",
            "-XX:AIExtensionUnit=" + TEST_LIB,
            Launcher.class.getName()
        ));
        args.addAll(List.of(testArgs));

        ProcessBuilder pb = ProcessTools.createLimitedTestJavaProcessBuilder(args);
        // setup ALT_AI_EXT_PATH for testing
        pb.environment().put("ALT_AI_EXT_PATH", System.getProperty("test.nativepath"));
        OutputAnalyzer output = ProcessTools.executeCommand(pb);
        output.shouldHaveExitValue(0).shouldContain("aiext_finalize\n");
        return output;
    }

    public static class Launcher {
        public static void main(String[] args) throws Exception {
            // Call the dispatch method to make classes to be loaded.
            dispatch(args);

            // Compile it with C2.
            int compLevel = CompilerWhiteBoxTest.COMP_LEVEL_FULL_OPTIMIZATION;
            Method m = Launcher.class.getDeclaredMethod("dispatch", String[].class);
            while (WHITE_BOX.getMethodCompilationLevel(m) != compLevel) {
                WHITE_BOX.enqueueMethodForCompilation(m, compLevel);
            }

            // Then call it again.
            dispatch(args);
        }

        private static void dispatch(String[] args) {
            switch (args[0]) {
                case "hello": {
                    hello();
                    break;
                }
                case "hello_int": {
                    hello(Integer.parseInt(args[1]));
                    break;
                }
                case "hello_long": {
                    hello(Long.parseLong(args[1]));
                    break;
                }
                case "hello_float": {
                    hello(Float.parseFloat(args[1]));
                    break;
                }
                case "hello_double": {
                    hello(Double.parseDouble(args[1]));
                    break;
                }
                case "hello_bytes": {
                    hello(args[1].getBytes());
                    break;
                }
                case "hello_object": {
                    hello(args[1]);
                    break;
                }
                case "hello_short_method": {
                    Launcher l = new Launcher();
                    l.hello(Short.parseShort(args[1]));
                    break;
                }
                case "add_ints": {
                    int result = add(Integer.parseInt(args[1]), Integer.parseInt(args[2]));
                    System.out.println(result);
                    break;
                }
                case "add_doubles": {
                    double result = add(Double.parseDouble(args[1]), Double.parseDouble(args[2]));
                    System.out.printf("%.2f\n", result);
                    break;
                }
                case "add_arrays": {
                    int[] a = new int[Integer.parseInt(args[1])];
                    Arrays.fill(a, Integer.parseInt(args[2]));
                    int[] b = new int[Integer.parseInt(args[3])];
                    Arrays.fill(b, Integer.parseInt(args[4]));

                    Launcher l = new Launcher();
                    l.add(a, b);

                    for (int i : a) {
                        System.out.println(i);
                    }
                    break;
                }
                default: {
                    throw new RuntimeException("Unknown test: " + args[0]);
                }
            }
        }

        private static void hello() {}
        private static void hello(int i) {}
        private static void hello(long l) {}
        private static void hello(float f) {}
        private static void hello(double d) {}
        private static void hello(byte[] chars) {}
        private static void hello(Object obj) {}
        private void hello(short s) {}

        private static int add(int a, int b) { return 0; }
        private static double add(double a, double b) { return 0; }
        private void add(int[] a, int[] b) {}
    }
}
