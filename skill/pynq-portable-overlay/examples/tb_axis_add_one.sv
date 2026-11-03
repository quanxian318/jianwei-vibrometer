module tb_axis_add_one;
    reg clk = 0, rst_n = 0;
    always #5 clk = ~clk;
    reg [31:0] in_data = 0;
    reg in_valid = 0, in_last = 0, out_ready = 0;
    wire in_ready, out_valid, out_last;
    wire [31:0] out_data;
    axis_add_one dut(clk, rst_n, in_data, in_valid, in_last, in_ready,
                    out_data, out_valid, out_last, out_ready);

    integer count, input_file, output_file, cycle = 0, received = 0;
    integer frame_words;
    reg [31:0] stalled_data;
    reg stalled_last, stalled = 0;
    string input_path, output_path;

    initial begin
        if (!$value$plusargs("input=%s", input_path) ||
            !$value$plusargs("output=%s", output_path) ||
            !$value$plusargs("count=%d", count) || count < 1)
            $fatal(1, "require +input +output +count");
        if (!$value$plusargs("frame=%d", frame_words)) frame_words = 7;
        if (frame_words < 1) $fatal(1, "frame must be positive");
        input_file = $fopen(input_path, "r");
        output_file = $fopen(output_path, "w");
        if (!input_file || !output_file) $fatal(1, "file open failed");
        repeat (3) @(negedge clk);
        rst_n = 1;
        for (int sent = 0; sent < count; sent++) begin
            @(negedge clk);
            if ($fscanf(input_file, "%h", in_data) != 1) $fatal(1, "short input");
            in_valid = 1;
            in_last = ((sent + 1) % frame_words == 0) || (sent + 1 == count);
            do @(posedge clk); while (!in_ready);
        end
        @(negedge clk);
        in_valid = 0;
        wait (received == count);
        repeat (5) @(negedge clk);
        $fclose(input_file);
        $fclose(output_file);
        $display("PASS protocol: %0d words, frame=%0d", count, frame_words);
        $finish;
    end

    always @(negedge clk) if (rst_n) begin
        cycle <= cycle + 1;
        out_ready <= (cycle % 5 >= 2);
        if (cycle > count * 8 + 30) $fatal(1, "protocol timeout");
    end

    always @(posedge clk) if (rst_n) begin
        if (stalled && (!out_valid || out_data !== stalled_data || out_last !== stalled_last))
            $fatal(1, "payload changed under backpressure");
        stalled <= out_valid && !out_ready;
        stalled_data <= out_data;
        stalled_last <= out_last;
        if (out_valid && out_ready) begin
            if (received >= count) $fatal(1, "unexpected extra word");
            if (out_last !== (((received + 1) % frame_words == 0) || (received + 1 == count)))
                $fatal(1, "incorrect TLAST at word %0d", received);
            $fdisplay(output_file, "%08x", out_data);
            received <= received + 1;
        end
    end
endmodule

