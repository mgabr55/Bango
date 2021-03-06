#include "CClient.h"
#include "CServer.h"


CClient::~CClient()
{
	if (m_pPlayer) {
		delete m_pPlayer;
	}

	while (m_Access.IsBusy()) {
		printf("CClient::~CClient: Client is in use, can't delete! Retrying in 10ms...\n");
		usleep(10000);
	}
}

bool CClient::Write(BYTE byType, ...)
{
	if (m_nCID == INVALID_SOCKET)
		return false;

	Packet packet;
	memset(&packet, 0, sizeof(Packet));

	packet.byType = byType;

	va_list va;
	va_start(va, byType);

	char* end = CSocket::WriteV(packet.data, va);

	va_end(va);

	packet.wSize = end - (char*)&packet;
	send(m_nCID, (char*)&packet, packet.wSize, MSG_NOSIGNAL);

	return true;
}

void CClient::Process(Packet packet)
{
	if (packet.byType >= C2S_START) 
	{
		if (m_pPlayer) {
			m_pPlayer->m_Access.Grant();
			m_pPlayer->Process(packet);

			// Double check in case player was deleted in CPlayer::Process.
			if(m_pPlayer)
				m_pPlayer->m_Access.Release();
		}
		else
			printf(KRED "CClient::Process: ERROR: Packet manipulation - player packet sent on not logged account.\n" KNRM);

		return;
	}

	if (m_pPlayer) {
		printf(KRED "CClient::Process: ERROR: Packet manipulation - client packet sent on logged account.\n" KNRM);
		return;
	}

	switch (packet.byType)
	{
		case C2S_CONNECT:
		{
			DWORD dwSeedL[4] = {0,};
			int nAppTime=0;
			BYTE byUnknown=0;

			CSocket::ReadPacket(packet.data, "dddddb", &nAppTime, &dwSeedL[0], &dwSeedL[1], &dwSeedL[2], &dwSeedL[3], &byUnknown);
			//printf("nAppTime: %i\ndwSeedL: %u %u %u %u\nbyUnknown: %u\n", nAppTime, dwSeedL[0], dwSeedL[1], dwSeedL[2], dwSeedL[3], byUnknown);

			DWORD dwProtocolVersion=0;
			BYTE byCode=0;
			int nTimeStamp=604800;// 1 week ;//0;
			int nTimeStart=0;
			DWORD dwSystem=0;
			DWORD dwEvent=0;
			BYTE byServerID=0;
			BYTE byAge=0;
			BYTE byCountry=N_EN;

			Write(S2C_CODE, "dbdddIbbb", dwProtocolVersion, byCode, nTimeStamp, nTimeStart, dwSystem, dwEvent, byServerID, byAge, byCountry);
			//printf("S2C_CODE sent.\n");
			break;
		}

		case C2S_ANS_CODE:
		{
			BYTE byUnknown=0;
			DWORD dwProtocolVersion=0;

			CSocket::ReadPacket(packet.data, "bd", &byUnknown, &dwProtocolVersion);
			//printf("byUnknown: %u\ndwProtocolVersion: %u\n", byUnknown, dwProtocolVersion);
			break;
		}

		case C2S_LOGIN:
		{
			char* szLogin=NULL;
			char* szPassword=NULL;
			char* szMac=NULL;

			CSocket::ReadPacket(packet.data, "sss", &szLogin, &szPassword, &szMac);
			//printf("Login: %s\nPassword: %s\nMac: %s\n", szLogin, szPassword, szMac);

			CDBSocket::Write(S2D_LOGIN, "dss", m_nCID, szLogin, szPassword);
			//printf("S2D_LOGIN sent.\n");
			break;
		}

		case C2S_SECOND_LOGIN:
		{
			BYTE byType=0;

			char *p = CSocket::ReadPacket(packet.data, "b", &byType);

			switch (byType)
			{
				case SL_CREATE_PASSWORD:
				{
					char *szPassword=NULL;
					char *szSecondaryPW=NULL;
					CSocket::ReadPacket(p, "ss", &szPassword, &szSecondaryPW);

					if (strlen(szSecondaryPW) != 8) {
						Write(S2C_CLOSE, "b", CC_KICK);
						break;
					}

					bool bQuit=false;
					for (char i = 0; i < 8; i++) {
						if (szSecondaryPW[i] < '0' || szSecondaryPW[i] > '9') {
							Write(S2C_CLOSE, "b", CC_KICK);
							bQuit=true; break;
						}
					}

					if (bQuit) break;

					printf("OnCreate PW: %s\nSecondary: %s\n", szPassword, szSecondaryPW);

					CDBSocket::Write(S2D_CREATE_SECONDARY, "dss", m_nCID, szPassword, szSecondaryPW);
					break;
				}

				case SL_CHANGE_PASSWORD:
				{
					char *szOldPassword=NULL;
					char *szNewPassword=NULL;

					CSocket::ReadPacket(p, "ss", &szOldPassword, &szNewPassword);

					if (strlen(szOldPassword) != 8 || strlen(szNewPassword) != 8) {
						Write(S2C_CLOSE, "b", CC_KICK);
						break;
					}

					bool bQuit=false;
					for (char i = 0; i < 8; i++) {
						if (szOldPassword[i] < '0' || szOldPassword[i] > '9' || szNewPassword[i] < '0' || szNewPassword[i] > '9') {
							Write(S2C_CLOSE, "b", CC_KICK);
							bQuit=true; break;
						}
					}

					if (bQuit) break;

					printf("Old password: %s\nNew password: %s\n", szOldPassword, szNewPassword);

					CDBSocket::Write(S2D_CHANGE_SECONDARY, "dss", m_nCID, szOldPassword, szNewPassword);
					break;
				}

				case SL_LOGIN: // Login
				{
					char *szPassword=NULL;
					CSocket::ReadPacket(p, "s", &szPassword);

					//printf("Secondary password: %s\n", szPassword);

					if (strlen(szPassword) != 8) {
						Write(S2C_CLOSE, "b", CC_KICK);
						break;
					}

					bool bQuit=false;
					for (char i = 0; i < 8; i++) {
						if (szPassword[i] < '0' || szPassword[i] > '9') {
							Write(S2C_CLOSE, "b", CC_KICK);
							bQuit=true; break;
						}
					}

					if (bQuit) break;

					CDBSocket::Write(S2D_SECONDARY_LOGIN, "ds", m_nCID, szPassword);

					break;
				}
			}
			break;
		}

		case C2S_DELPLAYER:
		{
			int nPID=0;

			CSocket::ReadPacket(packet.data, "d", &nPID);

			CDBSocket::Write(S2D_DELPLAYER, "dd", m_nCID, nPID);

			break;
		}

		case C2S_NEWPLAYER:
		{
			char* szName=NULL;
			BYTE byJob=0;
			WORD wStats[5]={0,};
			BYTE byShape[2]={0,};

			CSocket::ReadPacket(packet.data, "sbwwwwwbb", &szName, &byJob, &wStats[0], &wStats[1], &wStats[2], &wStats[3], &wStats[4], &byShape[0], &byShape[1]);
			
			if (std::string(szName).empty() || std::string(szName).length() > 14) {
				Write(S2C_ANS_NEWPLAYER, "b", NA_ERROR);
				break;
			}

			if (byJob < 0 || byJob > CLASS_NUM) {
				Write(S2C_ANS_NEWPLAYER, "b", NA_WRONGCLASS);
				break;
			}

			if (wStats[0] + wStats[1] + wStats[2] + wStats[3] + wStats[4] != 5) {
				Write(S2C_ANS_NEWPLAYER, "b", NA_WRONGPROPERTY);
				break;
			}

			if (byShape[0] < 0 || byShape[1] < 0 || byShape[0] > 6 || byShape[1] > 6) {
				Write(S2C_ANS_NEWPLAYER, "b", NA_WRONGPROPERTY);
				break;
			}

			CDBSocket::Write(S2D_NEWPLAYER, "dm", m_nCID, packet.data, packet.wSize - (packet.data - (char*)&packet));
			break;
		}

		case C2S_LOADPLAYER:
		{
			int nPID=0;
			int nGID=0;
			int nShow_honorGrade=0;

			CSocket::ReadPacket(packet.data, "ddd", &nPID, &nGID, &nShow_honorGrade);
			//printf("Load PID: %d, GID: %d, Show_honorGrade: %d\n", nPID, nGID, nShow_honorGrade);
			
			CDBSocket::Write(S2D_LOADPLAYER, "dd", m_nCID, nPID);
			break;
		}
	}
}

void CClient::OnLogin(char *p)
{
	BYTE byAnswer=0;

	p = CSocket::ReadPacket(p, "b", &byAnswer);

	if (byAnswer == LA_SAMEUSER) {
		int nClientExID=0;
		CSocket::ReadPacket(p, "d", &nClientExID);

		auto pClientEx = CServer::FindClient(nClientExID);
		if (pClientEx) {
			pClientEx->Write(S2C_CLOSE, "b", CC_SAMEUSER);
			pClientEx->m_Access.Release();
			::shutdown(nClientExID, SHUT_RDWR);
		}
		
	}

	Write(S2C_ANS_LOGIN, "b", byAnswer);

	//printf("S2C_ANS_LOGIN sent.\n");
}

char* CClient::OnLoadPlayer(char *p)
{
	D2S_LOADPLAYER_DESC desc;
	memset(&desc, 0, sizeof(D2S_LOADPLAYER_DESC));

	p = CSocket::ReadPacket(p, "ddsbbbwwwwwwwIwwwddddbb", 
		&desc.nAID, 
		&desc.nPID,
		&desc.szName,
		&desc.byClass,
		&desc.byJob,
		&desc.byLevel,
		&desc.wStats[P_STR], 
		&desc.wStats[P_HTH],
		&desc.wStats[P_INT],
		&desc.wStats[P_WIS], 
		&desc.wStats[P_DEX],
		&desc.wCurHP, 
		&desc.wCurMP, 
		&desc.n64Exp, 
		&desc.wPUPoint, 
		&desc.wSUPoint, 
		&desc.wContribute, 
		&desc.nAnger, 
		&desc.nX, 
		&desc.nY, 
		&desc.nZ,
		&desc.byFace,
		&desc.byHair);

	Lock();

	m_pPlayer = new CPlayer(m_nCID, desc);

	m_pPlayer->m_Access.Grant();

	Unlock();

	m_pPlayer->OnLoadPlayer();

	m_pPlayer->m_Access.Release();

	return p;
}

void CClient::OnLoadItems(char *p)
{
	if (!m_pPlayer) {
		printf(KRED "Cannot load items (non existing player)\n." KNRM);
		return;
	}

	m_pPlayer->m_Access.Grant();
	m_pPlayer->OnLoadItems(p);
	m_pPlayer->m_Access.Release();
}