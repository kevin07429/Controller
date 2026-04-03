def decrypt_log(file_path):
    key = "PowerOFF2026"  # 与C++代码中一致的密钥
    with open(file_path, "r", encoding="utf-8") as f:
        lines = f.readlines()
    
    for line in lines:
        line = line.strip()
        if not line: continue
        
        try:
            decrypted_bytes = bytearray()
            # 将按Hex存放的密文转回成原始字节并再次与密钥XOR解密
            for i in range(0, len(line), 2):
                hex_byte = line[i:i+2]
                char_code = int(hex_byte, 16)
                key_char = ord(key[(i // 2) % len(key)])
                decrypted_bytes.append(char_code ^ key_char)
            # 使用gbk解码原始字节流（因为C++端本地环境通常为GBK编码）
            print(decrypted_bytes.decode('gbk', errors='ignore'))
        except Exception as e:
            print(f"解密出错或非加密行: {line} - {e}")

if __name__ == "__main__":
    # 替换为你实际的日志路径
    decrypt_log("PowerOFF_Log.txt")