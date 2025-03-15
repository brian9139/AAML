`include "systolic.v"
`include "pe.v"
module TPU 
#(
    parameter ADDR_BITS=12
)
(
    clk,
    rst_n,

    in_valid,
    K,
    M,
    N,
    busy,

    A_wr_en,
    A_index,
    A_data_in,
    A_data_out,

    B_wr_en,
    B_index,
    B_data_in,
    B_data_out,

    C_wr_en,
    C_index,
    C_data_in,
    C_data_out
);


input clk;
input rst_n;
input            in_valid;
input [7:0]      K;
input [7:0]      M;
input [7:0]      N;
output  reg      busy;

output           A_wr_en;
output [ADDR_BITS-1:0]    A_index;
output [31:0]    A_data_in;
input  [31:0]    A_data_out;

output           B_wr_en;
output [ADDR_BITS-1:0]    B_index;
output [31:0]    B_data_in;
input  [31:0]    B_data_out;

output           C_wr_en;
output [ADDR_BITS-1:0]    C_index;
output [127:0]   C_data_in;
input  [127:0]   C_data_out;



//* Implement your design here

// 先把 K, M, N 放到 reg，不然 K, M, N 的值只會存在一個 cycle
reg [7:0] K_reg, M_reg, N_reg;


wire [1:0] state, n_state;
wire [31:0] counter;
wire [31:0] datain_h, datain_v;
wire [127:0] psum_1, psum_2, psum_3, psum_4;



// 定義 4 個 state，一般、讀取 buffer 資料、寫入 buffer 資料、結束
parameter IDLE = 2'd0;
parameter READ = 2'd1;
parameter WRITE = 2'd2;
parameter FINISH = 2'd3;

// busy signal，本想寫在 controller 裡面，但是線接回來的話，會造成 tpu busy signal 晚一個 cycle，造成錯誤
always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        busy <= 0;
    end else if (in_valid) begin
        busy <= 1;
    end else if (n_state == FINISH) begin
        busy <= 0;
    end
end



always @(posedge clk) begin
    if(K > 0) begin // 這邊要加上 K > 0，不然會有問題
        K_reg <= K;
        M_reg <= M;
        N_reg <= N;
    end
end


// 1. 用 data_loader 將資料載入 PE
data_loader A_loader(
    .clk(clk),
    .rst_n(rst_n),
    .state(state),
    .in_data(A_data_out),
    .K_reg(K_reg), 
    .counter(counter),
    .out_wire(datain_h)
);

data_loader B_loader(
    .clk(clk),
    .rst_n(rst_n),
    .state(state),
    .in_data(B_data_out),
    .K_reg(K_reg),
    .counter(counter),
    .out_wire(datain_v)
);
// 2. 用 systolic_array 進行計算
systolic_array systolic(
    .clk(clk),
    .rst_n(rst_n),
    .state(state),
    .datain_h(datain_h), // 橫向資料，即 A
    .datain_v(datain_v), // 縱向資料，即 B
    .psum_1(psum_1),
    .psum_2(psum_2),
    .psum_3(psum_3),
    .psum_4(psum_4)
);
// 3. 用 controller 控制 PE 和 data_loader 的狀態以及寫入資料到 Buffer C
controller ctrl(
    .clk(clk),
    .rst_n(rst_n),
    .in_valid(in_valid),
    .busy(busy),
    .K_reg(K_reg),
    .M_reg(M_reg),
    .N_reg(N_reg),
    .psum_1(psum_1),
    .psum_2(psum_2),
    .psum_3(psum_3),
    .psum_4(psum_4),
    .state_wire(state),
    .n_state_wire(n_state),
    .A_wr_en(A_wr_en),
    .B_wr_en(B_wr_en),
    .C_wr_en(C_wr_en),
    .A_data_in(A_data_in),
    .B_data_in(B_data_in),
    .C_data_in(C_data_in),
    .A_index(A_index),
    .B_index(B_index),
    .C_index(C_index),
    .counter_wire(counter)
);

endmodule



// 將資料載入PE，部分延遲載入
module data_loader(
    clk,
    rst_n,
    state,
    in_data,
    K_reg, 
    counter, 
    out_wire
);
    input clk;
    input rst_n;
    input [1:0] state;
    input [31:0] in_data;
    input [7:0] K_reg;
    input [31:0] counter;

    output wire [31:0] out_wire;

    localparam IDLE = 2'b0;
    localparam READ = 2'b1;

    reg [31:0] out;
    reg [7:0] temp_out1;
    reg [15:0] temp_out2;
    reg [23:0] temp_out3;
    reg [31:0] temp_out4;

    assign out_wire = (state==READ)? out : 32'b0;

    always @(posedge clk or negedge rst_n) begin
        if(!rst_n) begin
            temp_out1 <= 0;
            temp_out2 <= 0;
            temp_out3 <= 0;
            temp_out4 <= 0;
            out <= 0;
        end else if(state == IDLE) begin
            temp_out1 <= 0;
            temp_out2 <= 0;
            temp_out3 <= 0;
            temp_out4 <= 0;
            out <= 0;
        end  else if(state == READ) begin
            if(counter < K_reg) begin
                temp_out1 <= in_data[31:24];
                temp_out2 <= {temp_out2[7:0] , in_data[23:16]}; // 向前 shift 8 bits，並將新資料放入
                temp_out3 <= {temp_out3[15:0] , in_data[15:8]};
                temp_out4 <= {temp_out4[23:0] , in_data[7:0]};
                out <= {in_data[31:24], temp_out2[7:0], temp_out3[15:8], temp_out4[23:16]}; // 各自取開頭 8 bits，並組合，放在這的原因是這樣才能馬上取得資料，不然會延遲一個 cycle (所以上面的 out 才要註解)
            end
            else begin
                temp_out1 <= temp_out1 << 8;
                temp_out2 <= temp_out2 << 8;
                temp_out3 <= temp_out3 << 8;
                temp_out4 <= temp_out4 << 8;
                out <= {8'b0, temp_out2[7:0], temp_out3[15:8], temp_out4[23:16]}; 
            end
        end
    end
endmodule


// 控制信號變化，以及狀態轉換和資料寫入
// 原本資料寫入要多開一個 module，但大多的資料都在這，還要另外接線出去，會很麻煩，加上時序也可能造成一些問題，所以直接在這寫
module controller
#(
    parameter ADDR_BITS=12
)
(
    clk,
    rst_n,
    in_valid,
    busy,
    K_reg,
    M_reg,
    N_reg,
    psum_1,
    psum_2,
    psum_3,
    psum_4,
    state_wire,
    n_state_wire,
    A_wr_en,
    B_wr_en,
    C_wr_en,
    A_data_in,
    B_data_in,
    C_data_in,
    A_index,
    B_index,
    C_index,
    counter_wire
);
    input clk;
    input rst_n;
    input in_valid;
    input [7:0] K_reg;
    input [7:0] M_reg;
    input [7:0] N_reg;
    input [127:0] psum_1;
    input [127:0] psum_2;
    input [127:0] psum_3;
    input [127:0] psum_4;
    input busy;

    output wire [1:0] state_wire;
    output wire [1:0] n_state_wire;
    output A_wr_en;
    output B_wr_en;
    output C_wr_en;
    output [31:0] A_data_in;
    output [31:0] B_data_in;
    output [127:0] C_data_in;
    output [ADDR_BITS-1:0] A_index;
    output [ADDR_BITS-1:0] B_index;
    output [ADDR_BITS-1:0] C_index;
    output wire [31:0] counter_wire;

    // 狀態和下一個狀態
    reg [1:0] state;
    reg [1:0] n_state;
    
    reg [2:0] out_cycle; // 輸出 cycle 數，通常是 4，在最後一個 block 的則可能是 1~4

    // block offset
    reg [7:0] a_offset;
    reg [7:0] b_offset;

    // counter 系列
    reg [7:0] counter_a;
    reg [7:0] counter_b;
    reg [31:0] counter;
    reg [31:0] counter_out; // 注意要 32 bits, 只用 2 bit 會出問題

    // buffer index
    reg [15:0] idx_a;
    reg [15:0] idx_b;
    reg [15:0] idx_c;

    // 定義狀態
    parameter IDLE = 2'd0;
    parameter READ = 2'd1;
    parameter WRITE = 2'd2;
    parameter FINISH = 2'd3;

    // assign 給外部
    assign state_wire = state;
    assign n_state_wire = n_state;
    assign counter_wire = counter;
    assign A_index = (state==READ)? idx_a : 0;
    assign B_index = (state==READ)?idx_b : 0;
    assign C_index = idx_c;


    always @(posedge clk or negedge rst_n) begin
        if(!rst_n) 
            state <= IDLE;
        else 
            state <= n_state;
    end

    // state transition
    always @(*) begin
        case (state)
            IDLE: 
                n_state = (in_valid || busy) ? READ : IDLE;
            READ: 
                n_state = (counter <= (K_reg + 6)) ? READ : WRITE; 
            WRITE: 
                n_state = (counter_out < out_cycle) ? WRITE : (counter_b == b_offset) ? FINISH : IDLE;
            FINISH: 
                n_state = IDLE;
            default: 
                n_state = IDLE;
        endcase
    end

    // block offset
    always @(*) begin
        a_offset = ((M_reg+3)/4); // 相當於 $ceil(M/4.0)， 為 a 的 block 數量 (block 數是指資料被切成幾個 k*4 的區塊)
        b_offset = ((N_reg+3)/4); // $ceil(N/4)，相當於 b 的 block 數量
    end


    /* 
    ***********************************************************
    *   Control Signals                                       *
    *  - wr_en                                                *
    *  - out_cycle                                            *
    *  - counter , counter_a , counter_b , counter_out        *
    ***********************************************************
    */



    // wr_en
    assign A_wr_en = 0;
    assign B_wr_en = 0;
    assign C_wr_en = (n_state == WRITE) ? 1 : 0;

    // out_cycle
    always @(posedge clk or negedge rst_n) begin
        if(!rst_n) out_cycle <= 0;
        else if(state == busy)
            out_cycle <= (counter_a == (a_offset - 1) && M_reg[1:0] != 2'b00 ) ? M_reg[1:0] : 4; // 輸出 cycle 數，通常是 4，在最後一個 block 的則可能是 1~4
        else
            out_cycle <= out_cycle;
    end

    // counter
    always @(posedge clk or negedge rst_n) begin
        if(!rst_n) counter <= 0;
        else begin
            if(state == READ)
                counter <= counter + 1;
            else
                counter <= 0;
        end
    end

    // counter_a，用來表示現在是第幾個 block of A
    always @(posedge clk or negedge rst_n) begin
        if(!rst_n) counter_a <= 0;
        else begin
            if(counter_out == out_cycle-1 && state == WRITE) begin
                if(counter_a < a_offset)
                    counter_a <= counter_a + 1;
                else
                    counter_a <= 1;
            end
        else if(state == FINISH)
            counter_a <= 0;
        end
    end

    // counter_b，用來表示現在是第幾個 block of B
    // 注意，和 A 不同，A 每個 block 依序都和 B 的第一個 block 運算完後，B 才會換下一個 block，而 A 的 block 又從頭開始，以此類推
    always @(posedge clk or negedge rst_n) begin
        if(!rst_n) counter_b <= 0;
        else begin
            if(n_state == IDLE && counter_a == a_offset && busy)
                counter_b <= counter_b + 1;
            else if(state == FINISH)
                counter_b <= 0;
        end
    end

    // counter_out，用來表示現在是第幾個 output cycle
    always @(posedge clk or negedge rst_n) begin
        if(!rst_n) counter_out <= 0;
        else if(state == WRITE)
            counter_out <= counter_out + 1;
        else
            counter_out <= 0;
    end

    /* 
    ***********************************************************
    *   Buffer Index                                          *
    *  - The buffer location where data store or write        *
    *       - idx_a , idx_b , idx_c                           *
    *  - Wrte data                                            *
    *       - A_data_in , B_data_in , C_data_in               *
    ***********************************************************
    */

    // idx_a，取 buffer A 的資料位址
    always @(posedge clk or negedge rst_n) begin
        if(!rst_n) idx_a <= 15'd0;
        else begin
        if(state == WRITE) begin
            if(K_reg == 1)
                idx_a <= 1;
            else if(n_state==IDLE) begin
                idx_a <= idx_a + 15'd1;
            end
            else
              idx_a <= idx_a;
        end
        else if(state == IDLE && counter_a == a_offset)begin
            idx_a <= 0;
        end
        else if(state == FINISH)begin
            idx_a <= 15'd0;
        end
        else if(state == READ) begin
            if(counter < K_reg - 1) // 如果單純用 < k 的話，第一波運算會多讀下一波的第一筆資料，造成錯誤
            idx_a <= idx_a + 15'd1;
        end
        else begin
            idx_a <= idx_a;
        end
        end
    end
  
    // idx_b，用來取 buffer B 的資料位址
    always @(posedge clk or negedge rst_n) begin
        if(!rst_n) idx_b <= 0;
        else begin
            if(state == IDLE && busy) begin
                idx_b <= K_reg * counter_b; // 這邊要乘上 K_reg，不然會一直讀取同一個 block 的資料
            end
            else if(state == FINISH)begin
                idx_b <= 15'd0;
            end
            else if(state == READ) begin
                if(counter < K_reg ) // not K-1
                    idx_b <= idx_b + 1;
            end
        end
    end

    // idx_c，寫入 buffer C 的資料位址
    always @(posedge clk or negedge rst_n) begin
        if(!rst_n) idx_c <= 0;
        else begin
            if(state == FINISH)
                idx_c <= 0;
            else if(state == WRITE && n_state == WRITE)
                idx_c <= idx_c + 1;
            else
                idx_c <= idx_c;
        end
    end

    // 給 A, B, C 的資料
    assign A_data_in = 0;
    assign B_data_in = 0;

    // 依 row 的順序從 systolic array 取出 psum 給 C
    assign C_data_in = (!rst_n) ? 0 :
                   (counter_out == 2'd0) ? psum_1 :
                   (counter_out == 2'd1) ? psum_2 :
                   (counter_out == 2'd2) ? psum_3 :
                   (counter_out == 2'd3) ? psum_4 : 0;


endmodule
