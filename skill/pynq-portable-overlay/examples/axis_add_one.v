// One registered AXIS stage. Full words only; preserve frame boundaries.
module axis_add_one (
    input wire aclk, input wire aresetn,
    input wire [31:0] s_tdata, input wire s_tvalid, input wire s_tlast,
    output wire s_tready,
    output reg [31:0] m_tdata, output reg m_tvalid, output reg m_tlast,
    input wire m_tready
);
assign s_tready = aresetn && (!m_tvalid || m_tready);
always @(posedge aclk) begin
    if (!aresetn) begin
        m_tvalid <= 1'b0;
        m_tdata <= 32'd0;
        m_tlast <= 1'b0;
    end else if (s_tready) begin
        m_tvalid <= s_tvalid;
        if (s_tvalid) begin
            m_tdata <= s_tdata + 32'd1;
            m_tlast <= s_tlast;
        end
    end
end
endmodule

