// GetIP.cpp : Defines the entry point for the console application.
//

#include <tchar.h>
#include <string>

#include <stdio.h>

#if 0
/**
 * ����һ��ip���ҵ�����¼��ƫ�ƣ�����һ��IPLocation�ṹ
 * @param offset ���Ҽ�¼����ʼƫ��
 * @return IPLocation����
 */
private IPLocation getIPLocation(long offset) {
	try {
		// ����4�ֽ�ip
		ipFile.seek(offset + 4);
		// ��ȡ��һ���ֽ��ж��Ƿ��־�ֽ�
		byte b = ipFile.readByte();
		if(b == REDIRECT_MODE_1) {
			// ��ȡ����ƫ��
			long countryOffset = readLong3();
			// ��ת��ƫ�ƴ�
			ipFile.seek(countryOffset);
			// �ټ��һ�α�־�ֽڣ���Ϊ���ʱ������ط���Ȼ�����Ǹ��ض���
			b = ipFile.readByte();
			if(b == REDIRECT_MODE_2) {
				loc.country = readString(readLong3());
				ipFile.seek(countryOffset + 4);
			} else
				loc.country = readString(countryOffset);
			// ��ȡ������־
			loc.area = readArea(ipFile.getFilePointer());
		} else if(b == REDIRECT_MODE_2) {
			loc.country = readString(readLong3());
			loc.area = readArea(offset + 8);
		} else {
			loc.country = readString(ipFile.getFilePointer() - 1);
			loc.area = readArea(ipFile.getFilePointer());
		}
		return loc;
	} catch (IOException e) {
		return null;
	}
}	

/**
 * ��offsetƫ�ƿ�ʼ����������ֽڣ�����һ��������
 * @param offset ������¼����ʼƫ��
 * @return �������ַ���
 * @throws IOException �������ַ���
 */
private String readArea(long offset) throws IOException {
	ipFile.seek(offset);
	byte b = ipFile.readByte();
	if(b == REDIRECT_MODE_1 || b == REDIRECT_MODE_2) {
		long areaOffset = readLong3(offset + 1);
		if(areaOffset == 0)
			return LumaQQ.getString("unknown.area");
		else
			return readString(areaOffset);
	} else
		return readString(offset);
}

/**
 * ��offsetλ�ö�ȡ3���ֽ�Ϊһ��long����ΪjavaΪbig-endian��ʽ������û�취
 * ������ôһ����������ת��
 * @param offset ��������ʼƫ��
 * @return ��ȡ��longֵ������-1��ʾ��ȡ�ļ�ʧ��
 */
private long readLong3(long offset) {
	long ret = 0;
	try {
		ipFile.seek(offset);
		ipFile.readFully(b3);
		ret |= (b3[0] & 0xFF);
		ret |= ((b3[1] << 8) & 0xFF00);
		ret |= ((b3[2] << 16) & 0xFF0000);
		return ret;
	} catch (IOException e) {
		return -1;
	}
}	

/**
 * �ӵ�ǰλ�ö�ȡ3���ֽ�ת����long
 * @return ��ȡ��longֵ������-1��ʾ��ȡ�ļ�ʧ��
 */
private long readLong3() {
	long ret = 0;
	try {
		ipFile.readFully(b3);
		ret |= (b3[0] & 0xFF);
		ret |= ((b3[1] << 8) & 0xFF00);
		ret |= ((b3[2] << 16) & 0xFF0000);
		return ret;
	} catch (IOException e) {
		return -1;
	}
}

/**
 * ��offsetƫ�ƴ���ȡһ����0�������ַ���
 * @param offset �ַ�����ʼƫ��
 * @return ��ȡ���ַ����������ؿ��ַ���
 */
private String readString(long offset) {
	try {
		ipFile.seek(offset);
		int i;
		for(i = 0, buf[i] = ipFile.readByte(); buf[i] != 0; buf[++i] = ipFile.readByte());
		if(i != 0) 
			return Utils.getString(buf, 0, i, "GBK");
	} catch (IOException e) {			
		log.error(e.getMessage());
	}
	return "";
}
#endif

static std::string readstring(size_t offset)
{
	try
	{

	}
	catch(...)
	{
	}

}


int _tmain()
{
	return 0;
}

