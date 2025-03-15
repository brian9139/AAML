module PE(
    clk,
    rst_n,
    state,
    in_west, // input from west
    in_north, // input from north

    out_east, // output to east
    out_south,
    psum // result
);  
    input clk , rst_n;
    input [1:0] state;
    input signed [7:0]  in_west , in_north;

    output reg [7:0] out_east , out_south;
    output wire [31:0] psum;

    reg [31:0] maccout;
    reg [31:0] west_c , north_c; // temporary storage for west and north

    wire signed [31:0] product;

    localparam IDLE = 2'd0;
    localparam READ = 2'd1;

    assign psum = maccout;
    assign product = in_west * in_north;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            maccout <= 0;
            out_east <= 0;
            out_south <= 0;
        end else if (state == READ) begin
            maccout <= maccout + product;
            out_east <= in_west;
            out_south <= in_north;
        end else if (state == IDLE) begin
            maccout <= 0;
        end
    end
endmodule
