"""Disconnect only benchmark UDP sessions after their owned processes stop.

Read ownership from Windows' native UDP table while the client is alive.
After termination, exclusively reclaim that client's source port and send
NetQuake's clc_disconnect to the benchmark target. Never reuse an occupied
port, and never send a server/admin command.
"""
import ctypes, socket, struct
from ctypes import wintypes

def owned_ipv4_endpoints(pid):
 dll=ctypes.WinDLL('iphlpapi');size=wintypes.ULONG(0)
 dll.GetExtendedUdpTable(None,ctypes.byref(size),False,socket.AF_INET,1,0)
 buf=ctypes.create_string_buffer(size.value)
 code=dll.GetExtendedUdpTable(buf,ctypes.byref(size),False,socket.AF_INET,1,0)
 if code:raise OSError(code,'GetExtendedUdpTable')
 data=buf.raw;count=struct.unpack_from('<I',data)[0];result=[]
 for i in range(count):
  address,port,owner=struct.unpack_from('<III',data,4+i*12)
  if owner==pid:
   result.append({'address':socket.inet_ntoa(struct.pack('<I',address)),'port':socket.ntohs(port&65535),'owner_pid':pid})
 return result

def disconnect(endpoints,target,local_ip):
 host,port=target.rsplit(':',1);packet=struct.pack('>II',0x00100000|9,65535)+b'\x02';sent=[]
 for item in endpoints:
  if item['port']==26002 or item['address'] not in ('0.0.0.0',local_ip):continue
  with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as udp:
   udp.setsockopt(socket.SOL_SOCKET,socket.SO_EXCLUSIVEADDRUSE,1)
   try:udp.bind((local_ip,item['port']))
   except OSError:continue
   udp.sendto(packet,(host,int(port)));sent.append(item)
 return sent
