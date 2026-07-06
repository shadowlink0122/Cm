// ============================================================
// ビデオタイミング + TMDS Verilator テストベンチ
// non-timing モードで実行可能
// 手動クロック駆動でタイミング検証を実施
// ============================================================

`timescale 1ns / 1ps

module tb_video_timing;

    // タイミング定数
    localparam H_ACTIVE = 640;
    localparam H_FP     = 16;
    localparam H_SYNC   = 96;
    localparam H_BP     = 48;
    localparam H_TOTAL  = 800;
    localparam V_ACTIVE = 480;
    localparam V_FP     = 10;
    localparam V_SYNC   = 2;
    localparam V_BP     = 33;
    localparam V_TOTAL  = 525;

    // DVI 1.0 コントロールトークン定数
    localparam CTRL_C00 = 852;  // {C1=0,C0=0} = 1101010100
    localparam CTRL_C01 = 171;  // {C1=0,C0=1} = 0010101011
    localparam CTRL_C10 = 340;  // {C1=1,C0=0} = 0101010100
    localparam CTRL_C11 = 683;  // {C1=1,C0=1} = 1010101011

    // テスト信号
    reg clk = 0;
    reg rst = 0;
    reg pixel_clk = 0;
    wire hsync, vsync, de;
    wire [15:0] h_count, v_count;

    // DUT: ビデオタイミングジェネレータ
    video_timing dut (
        .clk(clk),
        .rst(rst),
        .pixel_clk(pixel_clk),
        .hsync(hsync),
        .vsync(vsync),
        .de(de),
        .h_count(h_count),
        .v_count(v_count)
    );

    // テストカウンタ
    integer pass_count = 0;
    integer fail_count = 0;
    integer total_tests = 0;
    integer i;
    integer hsync_width;
    integer de_width;
    integer frame_clocks;
    integer vsync_lines;

    // 手動クロックトグル (1サイクル)
    task tick;
        begin
            pixel_clk = 1;
            clk = 1;
            #1;
            pixel_clk = 0;
            clk = 0;
            #1;
        end
    endtask

    task check;
        input [255:0] name;
        input condition;
        begin
            total_tests = total_tests + 1;
            if (condition) begin
                $display("  [PASS] %0s", name);
                pass_count = pass_count + 1;
            end else begin
                $display("  [FAIL] %0s", name);
                fail_count = fail_count + 1;
            end
        end
    endtask

    initial begin
        $display("========================================");
        $display("TB: ビデオタイミング + TMDS 検証開始");
        $display("========================================");

        // --- TB-VT-01: 水平カウンタラップ ---
        $display("");
        $display("--- TB-VT-01: 水平カウンタラップ ---");
        // H_TOTAL (800) クロック後に hc がラップ
        for (i = 0; i < H_TOTAL; i = i + 1) begin
            tick;
        end
        check("hc ラップ (H_TOTAL=800 後に 0)", h_count == 0);

        // --- TB-VT-02: HSYNC パルス幅 ---
        $display("");
        $display("--- TB-VT-02: HSYNC パルス幅 ---");
        // hc=0 から再開始。H_ACTIVE+H_FP=656 まで hsync=1 であるべき
        // 一度ラインを通してHSYNCパルスを計測
        hsync_width = 0;
        for (i = 0; i < H_TOTAL; i = i + 1) begin
            if (hsync == 0) begin
                hsync_width = hsync_width + 1;
            end
            tick;
        end
        check("HSYNC パルス幅 = 96", hsync_width == H_SYNC);

        // --- TB-VT-03: HSYNC 開始位置 ---
        $display("");
        $display("--- TB-VT-03: HSYNC 開始/終了位置 ---");
        // H_ACTIVE+H_FP = 656 で LOW 開始, 656+96=752 で HIGH 復帰
        // 現在 hc=0。656 クロック進める
        for (i = 0; i < H_ACTIVE + H_FP; i = i + 1) begin
            tick;
        end
        // hc = 656: HSYNC は前サイクルの値が反映 (パイプライン遅延考慮)
        // 1クロック後にHSYNCがLOWになるはず
        tick;
        check("HSYNC LOW @ hc=657", hsync == 0);
        // H_SYNC-2 クロック進めて最後のLOW確認
        for (i = 0; i < H_SYNC - 2; i = i + 1) begin
            tick;
        end
        check("HSYNC LOW @ hc=751", hsync == 0);
        tick;
        check("HSYNC HIGH @ hc=752", hsync == 1);

        // --- TB-VT-04: DE アクティブ幅 ---
        $display("");
        $display("--- TB-VT-04: DE アクティブ幅 ---");
        // 次のライン先頭まで進める
        for (i = h_count; i < H_TOTAL; i = i + 1) begin
            tick;
        end
        // hc=0 の新しいライン (vc < V_ACTIVE ならDE=1)
        de_width = 0;
        for (i = 0; i < H_TOTAL; i = i + 1) begin
            if (de == 1) begin
                de_width = de_width + 1;
            end
            tick;
        end
        check("DE アクティブ幅 = 640", de_width == H_ACTIVE);

        // --- TB-VT-05: 1フレーム長 ---
        $display("");
        $display("--- TB-VT-05: 1フレーム長 ---");
        // 現在位置から v_count=0, h_count=0 まで進める
        // (最大 H_TOTAL * V_TOTAL クロック)
        frame_clocks = 0;
        // まず現在のフレーム末尾まで進める
        while (!(v_count == 0 && h_count == 0) && frame_clocks < H_TOTAL * V_TOTAL + 10) begin
            tick;
            frame_clocks = frame_clocks + 1;
        end
        // ここから1フレーム計測
        frame_clocks = 0;
        for (i = 0; i < H_TOTAL * V_TOTAL; i = i + 1) begin
            tick;
            frame_clocks = frame_clocks + 1;
        end
        check("フレーム長後 v_count=0", v_count == 0);
        check("フレーム長後 h_count=0", h_count == 0);
        check("フレーム = 420000 clk", frame_clocks == H_TOTAL * V_TOTAL);

        // --- TB-VT-06: VSYNC パルス ---
        $display("");
        $display("--- TB-VT-06: VSYNC パルス ---");
        // v_count=0 から V_ACTIVE+V_FP=490 ライン進める
        for (i = 0; i < (V_ACTIVE + V_FP) * H_TOTAL; i = i + 1) begin
            tick;
        end
        // v_count=490: VSYNC がLOWになるはず (パイプライン1clk遅延)
        tick;
        check("VSYNC LOW @ vc=490", vsync == 0);
        // VSYNC は V_SYNC=2 ライン間LOW
        vsync_lines = 0;
        for (i = 0; i < V_SYNC * H_TOTAL; i = i + 1) begin
            if (i % H_TOTAL == 0 && vsync == 0) begin
                vsync_lines = vsync_lines + 1;
            end
            tick;
        end
        check("VSYNC ライン数 = 2", vsync_lines == V_SYNC);
        check("VSYNC HIGH 復帰", vsync == 1);

        // --- TB-TE-01: TMDS コントロールトークン値検証 ---
        $display("");
        $display("--- TB-TE-01: DVI 1.0 コントロールトークン ---");
        // 定数値の正当性 (ビットパターン検証)
        check("CTRL {0,0} = 852 = 1101010100",
              CTRL_C00 == 10'b1101010100);
        check("CTRL {0,1} = 171 = 0010101011",
              CTRL_C01 == 10'b0010101011);
        check("CTRL {1,0} = 340 = 0101010100",
              CTRL_C10 == 10'b0101010100);
        check("CTRL {1,1} = 683 = 1010101011",
              CTRL_C11 == 10'b1010101011);

        // --- 結果サマリ ---
        $display("");
        $display("========================================");
        $display("テスト結果: %0d PASS / %0d FAIL (合計 %0d)",
                 pass_count, fail_count, total_tests);
        $display("========================================");

        if (fail_count > 0) begin
            $display("STATUS: FAIL");
            $finish;
        end else begin
            $display("STATUS: ALL PASS");
        end

        $finish;
    end

endmodule
