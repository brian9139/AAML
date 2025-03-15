module systolic_array(
    clk,
    rst_n,
    state,
    datain_h,
    datain_v,
    psum_1,
    psum_2,
    psum_3,
    psum_4
);

    input clk;
    input rst_n;
    input [1:0] state;
    input [31:0] datain_h;
    input [31:0] datain_v;

    output wire [127:0] psum_1;
    output wire [127:0] psum_2;
    output wire [127:0] psum_3;
    output wire [127:0] psum_4;


    wire [31:0] psum [0:15]; // partial sum
    wire [95:0] dataout_h;
    wire [95:0] dataout_v;
    parameter WRITE = 2'd2;

    // 組合 psum 的數據，psum_1 是指第一個 row 的 psum 組合，以此類推
    assign psum_1 = (state == WRITE)?{psum[0], psum[1], psum[2], psum[3]} : 128'b0;
    assign psum_2 = (state == WRITE)?{psum[4], psum[5], psum[6], psum[7]} : 128'b0;
    assign psum_3 = (state == WRITE)?{psum[8], psum[9], psum[10], psum[11]} : 128'b0;
    assign psum_4 = (state == WRITE)?{psum[12], psum[13], psum[14], psum[15]} : 128'b0;



    // genvar 和 generate 反而比較麻煩，代碼也沒少多少，就直接寫了 16 個 PE
    // 順序 第一個 row  1 2 3 4，下一個 row 5 6 7 8
    /* 
      PE 的排列方式
       1 2 3 4
       5 6 7 8
       9 10 11 12
      13 14 15 16   */
    // dataout_h 是指橫向的資料 (A)，dataout_v 是指縱向的資料 (B)
    // 從 pe1 開始，dataout_h 和 ，dataout_v 會依 PE 順序往下傳遞，並且下一個輸出的 index 會是上一個 + 8

    PE pe1(
        .clk (clk),
        .rst_n (rst_n),
        .state (state),
        .in_west (datain_h[31:24]),
        .in_north (datain_v[31:24]),
        .out_east (dataout_h[7:0]),
        .out_south (dataout_v[7:0]),
        .psum (psum[0])
    );
    PE pe2(
        .clk (clk),
        .rst_n (rst_n),
        .state (state),
        .in_west (dataout_h[7:0]),
        .in_north (datain_v[23:16]),
        .out_east (dataout_h[15:8]),
        .out_south (dataout_v[15:8]),
        .psum (psum[1])
    );
    PE pe3(
        .clk (clk),
        .rst_n (rst_n),
        .state (state),
        .in_west (dataout_h[15:8]),
        .in_north (datain_v[15:8]),
        .out_east (dataout_h[23:16]),
        .out_south (dataout_v[23:16]),
        .psum (psum[2])
    );
    PE pe4(
        .clk (clk),
        .rst_n (rst_n),
        .state (state),
        .in_west (dataout_h[23:16]),
        .in_north (datain_v[7:0]),
        .out_east (), // 這個是最後一個，所以不需要 output
        .out_south (dataout_v[31:24]),
        .psum (psum[3])
    );

    // 下一個 ROW
    PE pe5(
        .clk (clk),
        .rst_n (rst_n),
        .state (state),
        .in_west (datain_h[23:16]),
        .in_north (dataout_v[7:0]),
        .out_east (dataout_h[31:24]),
        .out_south (dataout_v[39:32]),
        .psum (psum[4])
    );
    PE pe6(
        .clk (clk),
        .rst_n (rst_n),
        .state (state),
        .in_west (dataout_h[31:24]),
        .in_north (dataout_v[15:8]),
        .out_east (dataout_h[39:32]),
        .out_south (dataout_v[47:40]),
        .psum (psum[5])
    );
    PE pe7(
        .clk (clk),
        .rst_n (rst_n),
        .state (state),
        .in_west (dataout_h[39:32]),
        .in_north (dataout_v[23:16]),
        .out_east (dataout_h[47:40]),
        .out_south (dataout_v[55:48]),
        .psum (psum[6])
    );
    PE pe8(
        .clk (clk),
        .rst_n (rst_n),
        .state (state),
        .in_west (dataout_h[47:40]),
        .in_north (dataout_v[31:24]),
        .out_east (),
        .out_south (dataout_v[63:56]),
        .psum (psum[7])
    );

    // 下一個 ROW
    PE pe9(
        .clk (clk),
        .rst_n (rst_n),
        .state (state),
        .in_west (datain_h[15:8]),
        .in_north (dataout_v[39:32]),
        .out_east (dataout_h[55:48]),
        .out_south (dataout_v[71:64]),
        .psum (psum[8])
    );
    PE pe10(
        .clk (clk),
        .rst_n (rst_n),
        .state (state),
        .in_west (dataout_h[55:48]),
        .in_north (dataout_v[47:40]),
        .out_east (dataout_h[63:56]),
        .out_south (dataout_v[79:72]),
        .psum (psum[9])
    );
    PE pe11(
        .clk (clk),
        .rst_n (rst_n),
        .state (state),
        .in_west (dataout_h[63:56]),
        .in_north (dataout_v[55:48]),
        .out_east (dataout_h[71:64]),
        .out_south (dataout_v[87:80]),
        .psum (psum[10])
    );
    PE pe12(
        .clk (clk),
        .rst_n (rst_n),
        .state (state),
        .in_west (dataout_h[71:64]),
        .in_north (dataout_v[63:56]),
        .out_east (),
        .out_south (dataout_v[95:88]),
        .psum (psum[11])
    );

    // 下一個 ROW
    PE pe13(
        .clk (clk),
        .rst_n (rst_n),
        .state (state),
        .in_west (datain_h[7:0]),
        .in_north (dataout_v[71:64]),
        .out_east (dataout_h[79:72]),
        .out_south (),
        .psum (psum[12])
    );
    PE pe14(
        .clk (clk),
        .rst_n (rst_n),
        .state (state),
        .in_west (dataout_h[79:72]),
        .in_north (dataout_v[79:72]),
        .out_east (dataout_h[87:80]),
        .out_south (),
        .psum (psum[13])
    );
    PE pe15(
        .clk (clk),
        .rst_n (rst_n),
        .state (state),
        .in_west (dataout_h[87:80]),
        .in_north (dataout_v[87:80]),
        .out_east (dataout_h[95:88]),
        .out_south (),
        .psum (psum[14])
    );
    PE pe16(
        .clk (clk),
        .rst_n (rst_n),
        .state (state),
        .in_west (dataout_h[95:88]),
        .in_north (dataout_v[95:88]),
        .out_east (),
        .out_south (),
        .psum (psum[15])
    );

endmodule
