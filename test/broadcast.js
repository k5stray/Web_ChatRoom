/* 广播测试
依赖node.js环境，执行如下命令安装：
sudo apt install nodejs
sudo npm install ws

运行测试需先启动服务，再执行：
node broadcast.js
*/

const WebSocket = require('ws');

// ========== 配置项 ==========
const WS_URL = "ws://127.0.0.1:8888";
const CLIENT_NUM = 1200;        // 并发客户端数量
const SENDER_INDEX = 0;          // 指定第0号客户端发送广播消息
const TEST_MSG_COUNT = 200;      // 发送多少条测试消息
const sendIntervalMs = 500;      // 消息发送间隔
// ============================

let clients = [];
let latencyList = [];
let receivedCount = 0;

console.log(`开始创建 ${CLIENT_NUM} 个WebSocket连接...`);

for (let i = 0; i < CLIENT_NUM; i++) {
    const ws = new WebSocket(WS_URL);
    ws.id = i;

    ws.on('message', (data) => {
        try {
            const obj = JSON.parse(data);
            if(obj.type === "news" && obj.data0){
                const recvTs = Date.now();
                const latency = recvTs - obj.data0;
                latencyList.push(latency);
                receivedCount++;
            }
        }catch(e){
        }
    });

    ws.on('error', (err)=>{
        console.error(`client ${i} error:`,err.message);
    });

    clients.push(ws);
}

// 等待2秒全部连接就绪后开始发送广播
setTimeout(()=>{
    console.log("全部客户端就绪，开始发送测试消息");
    let sendCnt = 0;
    const timer = setInterval(()=>{
        if(sendCnt >= TEST_MSG_COUNT){
            clearInterval(timer);
            setTimeout(()=>{
                if(latencyList.length === 0){
                    console.log("未收到广播消息");
                    return;
                }
                const sum = latencyList.reduce((a,b)=>a+b,0);
                const avg = sum / latencyList.length;
                let max = 0;
                let min = Number.MAX_SAFE_INTEGER;
                for(const v of latencyList){
                    if(v > max) max = v;
                    if(v < min) min = v;
                }
                console.log("\n======延迟统计结果======");
                console.log(`消息样本量: ${latencyList.length}`);
                console.log(`平均延迟: ${avg.toFixed(2)} ms`);
                console.log(`最大延迟: ${max} ms`);
                console.log(`最小延迟: ${min} ms`);
            },3000);
            return;
        }
        // 客户端发出带时间戳的聊天消息
        const senderWs = clients[SENDER_INDEX];
        if(senderWs.readyState === WebSocket.OPEN){
            senderWs.send(JSON.stringify({
                type:"news",
                name: "name" + sendCnt,
                data0: Date.now()
            }));
        }
        sendCnt++;
    },sendIntervalMs);

}, 2000);
