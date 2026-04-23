// JSON分包接收处理函数
let streamBuffer = ""

function addLog(message) {
    console.log(message);
}

function handleParsedJson(obj) {
    console.log("处理解析后的JSON对象:", obj);
}

function processChunk(fragment){
	addLog('进入processChunk')
	streamBuffer += fragment
	let braceCount = 0
	let jsonStart = -1
	for(let i = 0; i < streamBuffer.length; i++){
		const ch = streamBuffer[i]
		
		if(ch === "{"){
			if(braceCount === 0){
				jsonStart = i
			}
			braceCount++
		}else if(ch === "}"){
			braceCount--
			if(braceCount === 0 && jsonStart !== -1){
				const jsonStr = streamBuffer.substring(jsonStart, i + 1)
				addLog("收到完整JSON:" + jsonStr)
				try{
					const obj = JSON.parse(jsonStr)
					handleParsedJson(obj)
				}catch(e){
					addLog("JSON解析失败:" + e)
				}
				streamBuffer = streamBuffer.substring(i + 1)
				i = -1
				jsonStart = -1
			}
		}
	}
	//缓冲区过大时清理防越界
	if (streamBuffer.length > 20000) {
		addLog("⚠ 清空超大缓冲区（异常保护）")
		streamBuffer = ""
		braceCount = 0
		jsonStart = -1
	}
}

// 模拟分包发送JSON数据
function simulateJSONSending() {
    const jsonData = {
        type: "radarData",
        deviceId: 1001,
        timestamp: Date.now(),
        presence: 1,
        heartRate: 72.5,
        breathRate: 16.2,
        motion: 0,
        rssi: -45,
        heartbeatWaveform: 120,
        breathingWaveform: 45,
        rawSignal: -25
    };
    
    const jsonString = JSON.stringify(jsonData);
    console.log("原始JSON数据:", jsonString);
    
    // 模拟分包发送
    const packetSize = 15; // 每包15个字符
    for (let i = 0; i < jsonString.length; i += packetSize) {
        const packet = jsonString.substring(i, Math.min(i + packetSize, jsonString.length));
        console.log(`发送分包 ${Math.floor(i/packetSize)+1}:`, packet);
        processChunk(packet);
    }
}

// 测试函数
console.log("=== 开始测试JSON分包接收处理 ===");
simulateJSONSending();
console.log("=== 测试完成 ===");