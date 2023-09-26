#include <Siv3D.hpp>//
#include <vector>
#include <random>
using namespace std;
using Pair = pair<double, int>;//Dijkstraで多用するためあらかじめ宣言


//近傍を宣言しておく プロトコルの関係上変更禁止
constexpr Point dxdy4[4] = { Point(0,-1),Point(1,0),Point(0,1),Point(-1,0) };
constexpr Point cross4[4] = { {-1,-1} ,{1,-1},{1,1},{-1,1} };
constexpr Point dxdy8[8] = { Point{-1,-1},Point{0,-1},Point{1,-1},Point{1,0},Point{1,1},Point{0,1},Point{-1,1},Point{-1,0} };

constexpr int ownWall = 0, eneWall = 1, castle = 2, None = 3;//box.bldの表現

//1マスが持つ情報を集めたもの
struct box {
	bool isPond;//池かどうか
	bool ene, own;//陣地について
	bool isClose;//囲まれているならtrue
	int bld;//建物
	int mason;//そこに職人がいるか (使わない可能性あり コンパイル時最適化で自動消去)
};

//試合の規定を保存する 状況はsitua
struct match {
	match() {
		id = 0;
		turns = 0;
		now_turn = 0;
		turn_sec = 0;
		w = 0;
		h = 0;
		mason = 0;
		first = 0;
		token = U"";
		url = U"";
	}
	int  id;
	int turns;
	int now_turn;
	int turn_sec;
	int w;
	int h;
	int mason;
	bool first;
	String token;
	URL url;
	int num_enewall;
	int num_enearea;
	int num_enecastle;
	int num_ownwall;
	int num_ownarea;
	int num_owncastle;
	int post_failed_cnt;
};
match current;

//試合状況
struct situa {
	Array<Point>owns;//味方の座標が入っている
	Array<Point>enes;//敵の座標が入っている
	Grid<box>boxes;//マスの集まり
	Grid<bool>eneborder;//解法された陣地と共に使用 陣地の境界を担う壁のみtrue
	Grid<bool>ownborder;//解法された陣地と共に使用 陣地の境界を担う壁のみtrue

	situa(int W, int H) {
		boxes = Grid<box>(W, H, box{ 0,0,0,0,None });
	}
};

/*範囲外チェック*/bool OK(Point p) {
	return p.x >= 0 && p.x < current.w && p.y >= 0 && p.y < current.h;
}
/*範囲外チェック*/bool OK(int y, int x) {
	return x >= 0 && x < current.w && y >= 0 && y < current.h;
}

/*メルセンヌツイスタの乱数 使わなかったのでコンパイル時最適化に任せる*/
std::random_device rd; std::mt19937 gen(rd());
int random(int low, int high)
{
	std::uniform_int_distribution<> dist(low, high);
	return dist(gen);
}

/*マウスが乗っている座標取得 範囲外はmin,maxで無理矢理範囲内にする*/
Point where() {
	Point ret = Cursor::Pos();
	ret.x -= 300 / max(current.h, current.w); ret.y -= 300 / max(current.h, current.w); ret = ret * max(current.h, current.w) / 600;
	return ret;
}

//プロトタイプ宣言
void GetMatch(situa& Situa);//exe起動初回時に試合の規定を得る
void GetInfo(situa& situa);//毎ターン始めに試合状況を得る
void solver_dijkstra(situa& Situa, Grid<int32>& Break, Grid<int32>& Create, Array<Point>& own_move_to, JSON& json);
void score_OpenedBox(situa& situa);
bool gameStart();

bool UIbreaktarget = 0, UIcreatetarget = 0;//目標の操作
void Visualizer(situa& situa, Array<Texture>& emoji, Grid<int>& breakUI, Grid<int>& craftUI, Array<Point>& own_move_to, Font& ui_font) {
	double delta_alp_sin = Periodic::Sine0_1(1.3s);
	bool UIerase = KeySpace.pressed(); bool isTabPressed = KeyTab.pressed();
	Rect{ 0,0, 25 * 25 + 24, 25 * 25 + 24 }.draw(Palette::Darkturquoise.withAlpha(255));//背景

	//各種ボタンの宣言と描画
	Rect erase_target = Rect{ 660 + 160 , 330 + 240, 150, 70 }.drawShadow(Vec2{ 7,7 }, 5, 0).draw((UIerase) ? Palette::Black : Palette::White);
	ui_font((UIerase) ? U"delete" : U"append").drawAt(30, erase_target.center(), ((UIerase) ? Palette::White : Palette::Black));

	Rect description = Rect{ 660 , 330 + 240, 150, 70 }.drawShadow(Vec2{ 7,7 }, 5, 0).draw(Palette::Darkgray);
	ui_font(U"post failure:{}"_fmt(current.post_failed_cnt)).drawAt(20, description.center(), Palette::White);

	int clicked_num = 0;
	if (Key1.pressed())clicked_num = 1; if (Key2.pressed())clicked_num = 2;
	if (Key3.pressed())clicked_num = 3; if (Key4.pressed())clicked_num = 4;
	if (Key5.pressed())clicked_num = 5; if (Key6.pressed())clicked_num = 6;
	if (clicked_num > current.mason)clicked_num = 0;
	Rect own_move_cancel = Rect{ 660 + 160 , 330, 150, 230 }.draw(Palette::Black);
	ui_font(U"cancel").drawAt(30, own_move_cancel.center(), (Palette::White));
	if (clicked_num != 0 && MouseL.pressed())own_move_to[clicked_num - 1] = where();
	for (int i = 0; i < current.mason; i++) {
		Rect own_rect;
		if (clicked_num == i + 1)
			own_rect = Rect{ 660 , 330 + i * 240 / current.mason, 150, 240 / current.mason - 10 }.draw(Palette::Lightblue.withAlpha(100));
		else
			own_rect = Rect{ 660 , 330 + i * 240 / current.mason, 150, 240 / current.mason - 10 }.draw(Palette::Darkblue.withAlpha(100));
		if (own_move_to[i].x == -1 && own_move_to[i].y == -1)
			ui_font(U"{}:Auto"_fmt(i + 1)).drawAt(20, own_rect.center(), Palette::White);
		else
			ui_font(U"{}:{}"_fmt(i + 1, own_move_to[i])).drawAt(20, own_rect.center(), Palette::White);
	}
	if (clicked_num != 0) {

		if (own_move_cancel.leftPressed() || KeyBackspace.pressed())
			own_move_to[clicked_num - 1] = { -1,-1 };
	}

	//GUIによる優先度追加
	if (where().x >= 0 && where().x < current.w && where().y >= 0 && where().y < current.h && clicked_num == 0) {
		if (!UIerase && MouseL.down()) {
			if (situa.boxes[where().y][where().x].bld == None)
				craftUI[where().y][where().x] = 255;
		}
		else if (UIerase && MouseL.down()) {
			craftUI[where().y][where().x] = 0;
		}
		if (!UIerase && MouseR.down()) {
			if (situa.boxes[where().y][where().x].bld == ownWall || situa.boxes[where().y][where().x].bld == eneWall)
				breakUI[where().y][where().x] = 255;
		}
		else if (UIerase && MouseR.down()) {
			breakUI[where().y][where().x] = 0;

		}
	}

	//ここから盤面の描画 担当してないので分かってません
	double asp = 25.0 / max(current.h, current.w);
	int size = 22 * asp;
	int ippen = 24 * asp;
	for (int i = 1; i <= current.h; i++) {
		for (int j = 1; j <= current.w; j++) {

			Color color = Palette::Blue;
			int drawx = ippen * j;
			int drawy = ippen * i;
			box inbox = situa.boxes[i - 1][j - 1];

			//陣地の描画
			if (isTabPressed) {

				uint32 alp = 140;
				int bg_white_alpha = 255;
				if (!inbox.own && !inbox.ene) { alp = 0, bg_white_alpha = 0; }
				else if (!inbox.own && inbox.ene) { color = Palette::Red, alp = 0, bg_white_alpha = 0; }
				else if (inbox.own && !inbox.ene) {
					color = Palette::Blue;
					if (inbox.isClose)alp = 180;
				}
				else if (inbox.own && inbox.ene) {
					color = Palette::Purple;
					if (inbox.isClose)alp = 180;
				}
				Rect{ Arg::center(drawx,drawy),size,size }.draw(Palette::White.withAlpha(bg_white_alpha));
				Rect{ Arg::center(drawx,drawy),size,size }.draw(color.withAlpha(alp));
			}
			else {
				if (inbox.isPond) {
					int alp = delta_alp_sin * 120;
					int bg_white_alpha = 255 * delta_alp_sin;
					if (!inbox.own && !inbox.ene)alp = 0, bg_white_alpha = 0;
					else if (!inbox.own && inbox.ene)color = Palette::Red;
					else if (inbox.own && !inbox.ene)color = Palette::Blue;
					else if (inbox.own && inbox.ene)color = Palette::Purple;
					if (inbox.isClose)alp = delta_alp_sin * 180;
					Rect{ Arg::center(drawx,drawy),size,size }.draw(Palette::White.withAlpha(bg_white_alpha));
					Rect{ Arg::center(drawx,drawy),size,size }.draw(color.withAlpha(alp));
				}
				else {
					int alp = 140;
					if (!inbox.own && !inbox.ene)color = Color{ 75,100,75 }, alp = 255;
					else if (!inbox.own && inbox.ene)color = Palette::Red;
					else if (inbox.own && !inbox.ene)color = Palette::Blue;
					else if (inbox.own && inbox.ene)color = Palette::Purple;
					if (inbox.isClose)alp = 180;
					Rect{ Arg::center(drawx,drawy),size,size }.draw(Palette::White);
					Rect{ Arg::center(drawx,drawy),size,size }.draw(color.withAlpha(alp));
				}
			}


			//壁の描画
			if (situa.boxes[i - 1][j - 1].bld == eneWall && !isTabPressed)
				emoji[4].scaled(0.16 * asp).drawAt(ippen * j, ippen * i);
			if (situa.boxes[i - 1][j - 1].bld == ownWall)
				emoji[3].scaled(0.16 * asp).drawAt(ippen * j, ippen * i);//味方は0,敵は1,城は2,無は3
			if (situa.boxes[i - 1][j - 1].bld == castle)
				emoji[5].scaled(0.16 * asp).drawAt(ippen * j, ippen * i);//味方は0,敵は1,城は2,無は3

			if (!isTabPressed) {
				//目標の描画
				int alp = breakUI[i - 1][j - 1];
				double r = sqrt(ippen * ippen) / 2;
				double width = ippen / 5.0;
				Shape2D::Cross(r, width, Vec2(ippen * j, ippen * i)).draw(Palette::Black.withAlpha(alp));
				alp = craftUI[i - 1][j - 1];
				r = ippen / 2 - 3;
				width = ippen / 5.0;
				Shape2D::Plus(r, width, Vec2(ippen * j, ippen * i)).draw(Palette::White.withAlpha(alp));
			}
		}
	}

	//職人と味方の番号の描画 Tabで確認可能
	for (int i = 0; i < current.mason; i++) {
		int cy = situa.enes[i].y + 1, cx = situa.enes[i].x + 1;
		if (!isTabPressed)
			emoji[0].scaled(0.16 * asp).drawAt(ippen * cx, ippen * cy);

		cy = situa.owns[i].y + 1, cx = situa.owns[i].x + 1;
		emoji[1].scaled(0.16 * asp).drawAt(ippen * cx, ippen * cy);


		Rect{ Arg::center(ippen * cx, ippen * cy),size,size }.draw(Palette::White.withAlpha(40));
		ui_font(i + 1).drawAt(0.8 * ippen, { ippen * cx, ippen * cy }, Palette::Black);
	}
	//GUI入力中どこがクリックされるのか表示しておく
	if (OK(where()))
		Rect{ Arg::center(ippen * where().x + ippen,ippen * where().y + ippen),size,size }.draw(Palette::Green.withAlpha(160));


	//職人の強制移動先を表示(プログラム順序の関係で離れている)
	if (clicked_num != 0) {
		if (own_move_to[clicked_num - 1].x != -1 && own_move_to[clicked_num - 1].y != -1) {
			int cx = own_move_to[clicked_num - 1].x + 1, cy = own_move_to[clicked_num - 1].y + 1;
			Rect{ Arg::center(ippen * cx, ippen * cy),size,size }.drawFrame(3, 0, Palette::Black);
		}
	}
	/*
	もっと見栄えを良くする
	味方ターンならturnの色を青にするとかだと更に良い
	*/
	//通信,ターン数,点数状況を描画予定
	Rect wall_simbol = Rect{ 660 + 240, 20, 70, 50 };
	emoji[3].scaled(0.5).drawAt(wall_simbol.center());

	Rect Area_simbol0 = Rect{ 660 + 160, 0 - 2, 70, 50 }; Rect Area_simbol1 = Rect{ 660 + 180 + 2, 20, 70, 50 };
	Rect Area_simbol2 = Rect{ 660 + 140 - 2, 20, 70, 50 }; Rect Area_simbol3 = Rect{ 660 + 160, 40 + 2, 70, 50 };
	emoji[4].scaled(0.5 / 3).drawAt(Area_simbol0.center()); emoji[4].scaled(0.5 / 3).drawAt(Area_simbol1.center());
	emoji[4].scaled(0.5 / 3).drawAt(Area_simbol2.center()); emoji[4].scaled(0.5 / 3).drawAt(Area_simbol3.center());
	Rect{ 660 + 185,35,20,20 }.draw(Palette::White); Rect{ 660 + 185,35,20,20 }.draw(Palette::Red.withAlpha(180));

	Rect castle_simbol = Rect{ 660 + 80, 20, 70, 50 };
	emoji[5].scaled(0.5).drawAt(castle_simbol.center());

	Rect point_simbol = Rect{ 660 , 20, 70, 50 };
	emoji[6].scaled(0.5).drawAt(point_simbol.center());

	Rect own_wall = Rect{ 660 + 240, 90, 70, 50 }.draw(Palette::Darkcyan);
	ui_font(current.num_ownwall).drawAt(20, own_wall.center(), Palette::White);
	Rect own_area = Rect{ 660 + 160, 90, 70, 50 }.draw(Palette::Darkcyan);
	ui_font(current.num_ownarea).drawAt(20, own_area.center(), Palette::White);
	Rect own_castle = Rect{ 660 + 80, 90, 70, 50 }.draw(Palette::Darkcyan);
	ui_font(current.num_owncastle).drawAt(20, own_castle.center(), Palette::White);
	Rect own_point = Rect{ 660, 90, 70, 50 }.draw(Palette::Darkcyan);
	const int num_own_point = current.num_ownwall + 3 * current.num_ownarea + 10 * current.num_owncastle;
	ui_font(num_own_point).drawAt(20, own_point.center(), Palette::White);

	Rect ene_wall = Rect{ 660 + 240, 150, 70, 50 }.draw(Palette::Darkorange);
	ui_font(current.num_enewall).drawAt(20, ene_wall.center(), Palette::White);
	Rect ene_area = Rect{ 660 + 160, 150, 70, 50 }.draw(Palette::Darkorange);
	ui_font(current.num_enearea).drawAt(20, ene_area.center(), Palette::White);
	Rect ene_castle = Rect{ 660 + 80, 150, 70, 50 }.draw(Palette::Darkorange);
	ui_font(current.num_enecastle).drawAt(20, ene_castle.center(), Palette::White);
	Rect ene_point = Rect{ 660, 150, 70, 50 }.draw(Palette::Darkorange);
	const int num_ene_point = current.num_enewall + 3 * current.num_enearea + 10 * current.num_enecastle;
	ui_font(num_ene_point).drawAt(20, ene_point.center(), Palette::White);

	Rect  advantage_rect = Rect{ 660, 210, 310, 50 }.draw(Palette::Darkorange);
	if (num_ene_point + num_own_point != 0)
		Rect{ 660, 210, 310 * num_own_point / (num_ene_point + num_own_point), 50 }.draw(Palette::Darkcyan);
	else
		Rect{ 660, 210, 155, 50 }.draw(Palette::Darkcyan);
	ui_font(U"ADVANTAGE {} : {}"_fmt(num_own_point, num_ene_point)).drawAt(20, advantage_rect.center(), Palette::White);

	Rect Turn_rect = Rect{ 660, 270, 310, 50 }.draw(Palette::Darkorange);
	Rect{ 660, 270, 310 * current.now_turn / current.turns, 50 }.draw(Palette::Darkcyan);
	ui_font(U"TURN: {} / {}"_fmt(current.now_turn, current.turns)).drawAt(20, Turn_rect.center(), Palette::White);

}

//手を求め,サーバーにPOSTする OpenSiv3Dの公式リファレンスを参照
void POST(situa& situa, Grid<int>& BreakUI, Grid<int>& CreateUI,Array<Point>&own_move_to) {
	uint64 start = Time::GetMillisecSinceEpoch();
	const HashTable<String, String> headers = { { U"Content-Type", U"application/json" } };
	const URL query_url = current.url + U"/" + ToString(current.id) + U"?token=" + current.token;
	JSON json;
	json[U"turn"] = current.now_turn + 1;
	solver_dijkstra(situa, BreakUI, CreateUI,own_move_to, json);
	Console << U"solve" << Time::GetMillisecSinceEpoch() - start;
	json.save(U"posted.json");
	const std::string data = json.formatUTF8();
	const FilePath saveFilePath = U"post_result.json";

	if (auto response = SimpleHTTP::Post(query_url, headers, data.data(), data.size(), saveFilePath))
	{
		if (response.isOK())
			Console << U"POST OK";
		else
		{
			Console << U"POST FAILED0";
			current.post_failed_cnt++;
		}
	}
	else
		Console << U"POST FAILED1";

	Console << U"Time of POST:" << Time::GetMillisecSinceEpoch() - start;
}
void target_correct(situa& situa, Grid<int>& BreakUI, Grid<int>& CreateUI) {
	for (int i = 0; i < current.h; i++) {
		for (int j = 0; j < current.w; j++) {
			if (BreakUI[i][j] && situa.boxes[i][j].bld != ownWall && situa.boxes[i][j].bld != eneWall)
				BreakUI[i][j] = 0;
			if (CreateUI[i][j] && situa.boxes[i][j].bld != None)
				CreateUI[i][j] = 0;
		}
	}
}
//Main関数
void Main()
{
	Scene::SetBackground(Palette::Black); Window::Resize(Size{ 980,650 });//画面サイズ
	Font ui_font{ 50 };//visualizerで使うfontを宣言 visualizer内部でやると重いので
	Array<Texture> emoji = {//盤面表現の絵文字
		Texture{ U"🦀"_emoji } ,Texture{U"🐟"_emoji },
		Texture{U"💧"_emoji} ,Texture{U"🟦"_emoji},
		Texture{U"🟥"_emoji},Texture{U"🏯"_emoji},Texture{U"⚒️"_emoji} };

	situa situa(0, 0);//試合状況の初期状態 とりあえずW,H=0,0

	GetMatch(situa);//試合の規定を得る

	Grid<int>BreakUI(current.w, current.h); Grid<int>CreateUI(current.w, current.h);
	Array<Point>own_move_to(current.mason, { -1,-1 });
	while (System::Update()) {
		if (gameStart())break;
		Visualizer(situa, emoji, BreakUI, CreateUI, own_move_to, ui_font);
	}
	for (; current.now_turn < current.turns + 1;) {
		GetInfo(situa);
		score_OpenedBox(situa);
		target_correct(situa, BreakUI, CreateUI);

		uint64 turn_start_sec = Time::GetMillisecSinceEpoch();//何秒経ったか記録する
		if ((current.first == true && current.now_turn % 2 == 0) || (current.first == false && current.now_turn % 2 == 1))
			Scene::SetBackground(Color{ 50,50,100 });
		else
			Scene::SetBackground(Color{ 100,50,50 });
		while (System::Update())
		{
			Visualizer(situa, emoji, BreakUI, CreateUI, own_move_to, ui_font);

			//自手の時はsolverのため早めに終わる
			if ((current.first == true && current.now_turn % 2 == 0) || (current.first == false && current.now_turn % 2 == 1)) {
				if (Time::GetMillisecSinceEpoch() - turn_start_sec > current.turn_sec * 1000UL - 300UL)//時間要調整
					break;
			}
			//自手じゃないので遅めに終わる ブラウザの更新と同期してるか確認しろ
			if (Time::GetMillisecSinceEpoch() - turn_start_sec > current.turn_sec * 1000UL - 100UL) {//時間要調整
				break;
			}
		}
		//自手なのでPOST
		if ((current.first == true && current.now_turn % 2 == 0) || (current.first == false && current.now_turn % 2 == 1)) {
			POST(situa, BreakUI, CreateUI,own_move_to);
		}
	}
}

//試合の規定を得る
void GetMatch(situa& Situa) {
	bool flag = true;//サーバーが立ち上がってルールのJSONを得られるまでtrueのflag
	current.now_turn = -1;
	current.post_failed_cnt = 0;
	const JSON player_json = JSON::Load(U"player.json");
	int match_number = player_json[U"parallel"].get<int32>();//同時並行では1の時もある
	current.token = player_json[U"token"].get<String>();
	current.url = player_json[U"url"].get<String>();


	const URL query_url = current.url + U"?token=" + current.token;	//const URL url = U"https://www.procon.gr.jp/matches?token="+token;
	const HashTable<String, String> headers = { { U"Content-Type", U"application/json" } };
	const FilePath saveFilePath = U"match_data.json";
	while (flag) {
		if (const auto response = SimpleHTTP::Get(query_url, headers, saveFilePath))
		{
			const JSON json = JSON::Load(saveFilePath);
			if (not json) throw Error{ U"JSONファイルの取得に失敗" };

			//試合情報一斉入力
			current.id = json[U"matches"][match_number][U"id"].get<int32>();
			current.first = json[U"matches"][match_number][U"first"].get<bool>();
			current.turn_sec = json[U"matches"][match_number][U"turnSeconds"].get<int32>();
			current.turns = json[U"matches"][match_number][U"turns"].get<int32>();
			current.h = json[U"matches"][match_number][U"board"][U"height"].get<int32>();
			current.w = json[U"matches"][match_number][U"board"][U"width"].get<int32>();
			Situa = situa(current.w, current.h);
			Situa.enes = Array<Point>(json[U"matches"][match_number][U"board"][U"mason"].get<int32>());
			Situa.owns = Array<Point>(json[U"matches"][match_number][U"board"][U"mason"].get<int32>());
			current.mason = json[U"matches"][match_number][U"board"][U"mason"].get<int32>();
			for (auto&& [key, value] : json[U"matches"][match_number][U"board"][U"structures"]) {
				for (auto&& [key2, value2] : value) {
					if (value2.get<int32>() == 1)//池
						Situa.boxes[Parse<int32>(key)][Parse<int32>(key2)].isPond = true;
					if (value2.get<int32>() == 2)//城
						Situa.boxes[Parse<int32>(key)][Parse<int32>(key2)].bld = castle;
				}
			}
			for (auto&& [key, value] : json[U"matches"][match_number][U"board"][U"masons"]) {
				for (auto&& [key2, value2] : value) {
					int now = value2.get<int32>();
					int y = Parse<int32>(key), x = Parse<int32>(key2);
					if (now != 0) {
						if (now > 0)Situa.owns[now - 1] = Point{ x,y };
						if (now < 0)Situa.enes[-now - 1] = Point{ x,y };
					}
					Situa.boxes[y][x].mason = now;
				}
			}

			flag = false;
			Console << U"サーバーとの接続に成功";
		}
		else { Console << U"サーバーとの疎通に失敗"; }
	}
}
bool gameStart() {
	const HashTable<String, String> headers = { { U"Content-Type", U"application/json" } };
	const FilePath saveFilePath = U"turn_data.json";
	const URL query_url = current.url + U"/" + ToString(current.id) + U"?token=" + current.token;
	if (const auto response = SimpleHTTP::Get(query_url, headers, saveFilePath))
	{
		const JSON json = JSON::Load(saveFilePath);
		if (not json) { Console << U"ターンJSON取得失敗";	return false; }
		return true;
	}
	return false;
}
//盤面情報の取得
void GetInfo(situa& Situa) {
	bool get_flag = true;
	const HashTable<String, String> headers = { { U"Content-Type", U"application/json" } };
	const FilePath saveFilePath = U"turn_data.json";
	const URL query_url = current.url + U"/" + ToString(current.id) + U"?token=" + current.token;
	while (get_flag) {
		if (const auto response = SimpleHTTP::Get(query_url, headers, saveFilePath))
		{
			const JSON json = JSON::Load(saveFilePath);
			if (not json) { Console << U"ターンJSON取得失敗"; continue; }

			//ターンが進んだことが確認できた
			if (json[U"turn"].get<int32>() == current.now_turn + 1)get_flag = false, current.now_turn++;
			else {
				//途中参加のパターン
				if (json[U"turn"].get<int32>() != current.now_turn) { current.now_turn = json[U"turn"].get<int32>(); }
				continue;
			};

			//盤面状況を得る
			for (auto&& [key, value] : json[U"board"][U"territories"]) {
				for (auto&& [key2, value2] : value) {
					int now = value2.get<int32>();
					int y = Parse<int32>(key), x = Parse<int32>(key2);
					if (now & 0b01)Situa.boxes[y][x].own = true;
					else Situa.boxes[y][x].own = false;
					if (now & 0b10)Situa.boxes[y][x].ene = true;
					else Situa.boxes[y][x].ene = false;

					Situa.boxes[y][x].isClose = false;
				}
			}
			for (auto&& [key, value] : json[U"board"][U"masons"]) {
				for (auto&& [key2, value2] : value) {
					int now = value2.get<int32>();
					int y = Parse<int32>(key), x = Parse<int32>(key2);
					if (now != 0) {
						if (now > 0)Situa.owns[now - 1] = Point{ x,y };
						if (now < 0)Situa.enes[-now - 1] = Point{ x,y };
					}
					Situa.boxes[y][x].mason = now;
				}
			}
			for (auto&& [key, value] : json[U"board"][U"walls"]) {
				for (auto&& [key2, value2] : value) {
					int now = value2.get<int32>();
					int y = Parse<int32>(key), x = Parse<int32>(key2);
					if (now == 0 && Situa.boxes[y][x].bld != castle)Situa.boxes[y][x].bld = None;
					if (now == 1)
						Situa.boxes[y][x].bld = ownWall;
					if (now == 2)
						Situa.boxes[y][x].bld = eneWall;
				}
			}
			Console << U"ターン取得成功";
		}
		else { Console << U"ターン疎通失敗"; }
	}
}

void DFS(situa& situa, Grid<bool>& visited, int y, int x, int wall) {
	if (visited[y][x])return;
	visited[y][x] = true;

	for (Point p : dxdy4) {
		int ny = y + p.y, nx = x + p.x;
		if (ny < 0 || ny >= current.h + 2 || nx < 0 || nx >= current.w + 2 || visited[ny][nx])continue;
		if (OK(ny - 1, nx - 1)) {
			if (situa.boxes[ny - 1][nx - 1].bld != wall)
				DFS(situa, visited, ny, nx, wall);
		}
		else {
			DFS(situa, visited, ny, nx, wall);
		}
	}
}

void score_OpenedBox(situa& situa) {
	current.num_enearea = 0; current.num_enecastle = 0; current.num_enewall = 0;
	current.num_ownarea = 0; current.num_owncastle = 0; current.num_ownwall = 0;

	//DFS
	Grid<bool>visited_own(current.w + 2, current.h + 2, 0);
	DFS(situa, visited_own, 0, 0, ownWall);
	Grid<bool>visited_ene(current.w + 2, current.h + 2, 0);
	DFS(situa, visited_ene, 0, 0, eneWall);

	situa.eneborder = Grid<bool>(current.w, current.h, 0);
	situa.ownborder = Grid<bool>(current.w, current.h, 0);
	for (int i = 0; i < current.h; i++) {
		for (int j = 0; j < current.w; j++) {
			if (situa.boxes[i][j].bld == ownWall) {
				current.num_ownwall++;
				situa.ownborder[i][j] = visited_own[i + 2][j + 1] || visited_own[i + 1][j + 2] || visited_own[i][j + 1] || visited_own[i + 1][j];
			}
			if (situa.boxes[i][j].bld == eneWall) {
				current.num_enewall++;
				situa.eneborder[i][j] = visited_ene[i + 2][j + 1] || visited_ene[i + 1][j + 2] || visited_ene[i][j + 1] || visited_ene[i + 1][j];
			}
			if (situa.boxes[i][j].own) {
				current.num_ownarea++;
				situa.boxes[i][j].isClose = !visited_own[i + 1][j + 1];
				if (situa.boxes[i][j].bld == castle)
					current.num_owncastle++;

			}
			if (situa.boxes[i][j].ene) {
				current.num_enearea++;
				situa.boxes[i][j].isClose = !visited_ene[i + 1][j + 1];
				if (situa.boxes[i][j].bld == castle)
					current.num_enecastle++;
			}
		}
	}
}
//dijkstra のreturnが複雑なので構造体に
struct dij_res {
	Point from;
	Point to;
	int32 dir;
	//最も近場の建築,破壊箇所は 座標fromの近傍toである
	//dirは職人の近傍に建築,破壊箇所があった場合のみ有効
};

//味方のDijkstra 誰ですかこんな地獄みたいな宣言考えたのは
dij_res own_dijkstra(situa& situa, Grid<double>& dist, Grid<bool>& footprint, Grid<bool>& ene_pos, Grid<bool>& own_pos, int sy, int sx, Grid<int32>& Break, Grid<int32>& Create) {
	dist = Grid<double>(current.w, current.h, -1);//最短経路を更新,メモするgrid
	priority_queue< Pair, vector<Pair>, greater<Pair>>  que;//Dijkstraのアルゴリズムを参照

	que.emplace((dist[sy][sx] = 0.0), sy * current.w + sx);

	while (!que.empty())
	{
		double d = que.top().first;
		int from = que.top().second;
		int fromx = from % current.w;
		int fromy = from / current.w;
		que.pop();

		// 最短距離でなければ処理しない
		if (dist[fromy][fromx] >= 0 && dist[fromy][fromx] < d)
			continue;

		int  tox, toy; double cost, nd;//to=近傍座標 cost=近傍への遷移コスト nd=現在から遷移した場合のtoのstart地点からの距離

		//縦横の近傍を見る
		for (int i = 0; i < 4; i++) {
			tox = fromx + dxdy4[i].x;
			toy = fromy + dxdy4[i].y;
			if (!OK(toy, tox))
				continue;

			if (Create[toy][tox]) {
				//近傍に敵がいる時は壁を建てられない
				if (!(d == 0.0 && ene_pos[toy][tox])) {
					return { {fromx,fromy},{tox,toy},i };//それ以外の時は最短で建築可能なのでreturn
				}
			}


			if (Break[toy][tox]) {
				return { {fromx,fromy},{tox,toy},i };//最短で破壊可能なのでreturn
			}

			//現在か1ターン前に敵がいるかつ近傍なら移動できない
			if (d == 0 && (ene_pos[toy][tox] || own_pos[toy][tox] || footprint[toy][tox]))continue;

			if (situa.boxes[toy][tox].isPond == true)continue;

			if (situa.boxes[toy][tox].bld == eneWall) {
				if (situa.eneborder[toy][tox] == false)continue;/*破壊が敵利益となる*/
				else cost = 1 / 1.0 + 1 / 2.0 + 1 / 4.0 + 1 / 8.0;//=1.875<2
			}
			else cost = 1.0;

			nd = dist[fromy][fromx] + cost;
			if (nd < dist[toy][tox] || dist[toy][tox] < 0.0)
				//if (dist[toy][tox] < 0.0)//未到達なら更新
				que.push({ dist[toy][tox] = nd,toy * current.w + tox });
		}

		//斜め
		for (int i = 0; i < 4; i++) {
			cost = 1.0;
			tox = fromx + cross4[i].x;
			toy = fromy + cross4[i].y;
			if (!OK(toy, tox))
				continue;

			if (situa.boxes[toy][tox].bld == eneWall || situa.boxes[toy][tox].isPond)
				continue;
			if (d == 0 && (ene_pos[toy][tox] || own_pos[toy][tox] || footprint[toy][tox])) {
				continue;
			}

			nd = dist[fromy][fromx] + cost;
			if (nd < dist[toy][tox] || dist[toy][tox] < 0.0)
				//if (dist[toy][tox] < 0)
				que.emplace((dist[toy][tox] = nd), toy * current.w + tox);
		}
	}
	//あってはならないが,優先度Create,Breakのどちらにも行くことが出来ない場合
	return { {-1,-1},{-1,-1} ,0 };
}
void move_dijkstra(situa& situa, Grid<double>& dist, Grid<bool>& footprint, Grid<bool>& ene_pos, Grid<bool>& own_pos, int sy, int sx, int gy, int gx) {
	dist = Grid<double>(current.w, current.h, -1);//最短経路を更新,メモするgrid
	priority_queue< Pair, vector<Pair>, greater<Pair>>  que;//Dijkstraのアルゴリズムを参照

	que.emplace((dist[sy][sx] = 0.0), sy * current.w + sx);

	while (!que.empty())
	{
		double d = que.top().first;
		int from = que.top().second;
		int fromx = from % current.w;
		int fromy = from / current.w;
		que.pop();
		if (fromx == gx && fromy == gy)return;

		// 最短距離でなければ処理しない
		if (dist[fromy][fromx] >= 0 && dist[fromy][fromx] < d)
			continue;

		int  tox, toy; double cost, nd;//to=近傍座標 cost=近傍への遷移コスト nd=現在から遷移した場合のtoのstart地点からの距離

		//縦横の近傍を見る
		for (int i = 0; i < 4; i++) {
			tox = fromx + dxdy4[i].x;
			toy = fromy + dxdy4[i].y;
			if (!OK(toy, tox))
				continue;

			//現在か1ターン前に敵がいるかつ近傍なら移動できない
			if (d == 0 && (ene_pos[toy][tox] || own_pos[toy][tox] || footprint[toy][tox]))continue;
			if (situa.boxes[toy][tox].isPond == true)continue;

			if (situa.boxes[toy][tox].bld == eneWall) {
				if (situa.eneborder[toy][tox] == false)continue;/*破壊が敵利益となる*/
				else cost = 1 / 1.0 + 1 / 2.0 + 1 / 4.0 + 1 / 8.0;//=1.875<2
			}
			else cost = 1.0;

			nd = dist[fromy][fromx] + cost;
			if (nd < dist[toy][tox] || dist[toy][tox] < 0.0)
				que.push({ dist[toy][tox] = nd,toy * current.w + tox });
		}

		//斜め
		for (int i = 0; i < 4; i++) {
			cost = 1.0;
			tox = fromx + cross4[i].x;
			toy = fromy + cross4[i].y;
			if (!OK(toy, tox))
				continue;

			if (situa.boxes[toy][tox].bld == eneWall || situa.boxes[toy][tox].isPond)
				continue;
			if (d == 0 && (ene_pos[toy][tox] || own_pos[toy][tox] || footprint[toy][tox])) {
				continue;
			}

			nd = dist[fromy][fromx] + cost;
			if (nd < dist[toy][tox] || dist[toy][tox] < 0.0)
				que.emplace((dist[toy][tox] = nd), toy * current.w + tox);
		}
	}
}
//手を表す構造体
struct action {
	int32 hand;//1.移動 2.建築 3.破壊
	int32 dir;//方向
};

void solver_dijkstra(situa& Situa, Grid<int32>& Break, Grid<int32>& Create, Array<Point>& own_move_to, JSON& json) {

	//目標がない関係でものすごく時間がかかる時は強制break (実装予定)

	Array<int32>perm(current.mason); for (int i = 0; i < current.mason; i++)perm[i] = i;
	Array<action>max_hands(current.mason, { 0,0 }); double max_hands_val = -10000;

	//permで順列全探索
	do {
		Array<action>now_hands(current.mason, { 0,0 }); double now_hands_val = 0;

		Grid<int32>inBreak = Break, inCreate = Create;
		situa inSitua = Situa;
		Grid<bool> ene_pos(current.w, current.h, false);//操作を決め打ちしている時の敵の座標
		Grid<bool>own_pos(current.w, current.h, false);//操作を決め打ちしている時の味方の座標
		Grid<bool>footprint(current.w, current.h, false);//操作前の職人(味方のみで良い)の座標
		for (Point& p : Situa.owns) { own_pos[p.y][p.x] = true; footprint[p.y][p.x] = true; }
		for (Point& p : Situa.enes) { ene_pos[p.y][p.x] = true; }

		for (int i = 0; i < current.mason; i++) {
			//職人iの座標
			int y = inSitua.owns[perm[i]].y, x = inSitua.owns[perm[i]].x;
			action act(0, 0);
			Grid<double>dist; Point from;
			//見つけることが出来なかった場合には-1のpairを返す
			if (!OK(own_move_to[perm[i]])) {
				dij_res res = own_dijkstra(inSitua, dist, footprint, ene_pos, own_pos, y, x, inBreak, inCreate);
				from = res.from;

				//事故って建築,破壊対象がない状態になってなければ
				if (res.from.x != -1) {
					//近傍に建築,破壊可能なものがあった時
					if (res.from.y == y && res.from.x == x) {
						if (inBreak[res.to.y][res.to.x]) {
							act = { 3,2 * (res.dir + 1) };
						}
						if (inCreate[res.to.y][res.to.x]) {
							act = { 2,2 * (res.dir + 1) };
						}
					}
					else {//移動 もしくは敵の壁を破壊して最短経路へ向かう時

						inBreak[res.to.y][res.to.x] = false;
						inCreate[res.to.y][res.to.x] = false;
						//fromからの経路復元を行う
						double d = dist[res.from.y][res.from.x];
						while (true) {
							//斜め
							bool get_process_flag = false;
							bool get_ans_flag = false;

							if (inSitua.boxes[res.from.y][res.from.x].bld != eneWall) {
								for (int i = 0; i < 4; i++) {
									int ny = res.from.y + cross4[i].y;
									int nx = res.from.x + cross4[i].x;
									if (!OK(ny, nx))continue;
									//currentを見つけた時 距離も確認
									if (ny == y && nx == x && d == 1.0) {

										get_ans_flag = true;
										int dir = 2 * i;
										dir = (dir + 4) % 8 + 1;//方向反転
										act = { 1, dir };
										break;
									}

									if (dist[ny][nx] == d - 1.0) {
										res.from.y = ny, res.from.x = nx;
										get_process_flag = true;
										d -= 1.0;
										break;
									}
								}
							}
							if (get_process_flag)continue;
							if (get_ans_flag)break;
							//縦横
							for (int i = 0; i < 4; i++) {
								int ny = res.from.y + dxdy4[i].y;
								int nx = res.from.x + dxdy4[i].x;
								if (!OK(ny, nx))continue;
								//currentを見つけた時
								if (ny == y && nx == x) {
									get_ans_flag = true;
									int dir = 2 * i + 1;
									dir = (dir + 4) % 8 + 1;//方向反転
									if (inSitua.boxes[res.from.y][res.from.x].bld == eneWall)
										act = { 3, dir };
									else
										act = { 1, dir };
									break;
								}

								if (inSitua.boxes[res.from.y][res.from.x].bld == eneWall) {
									if (dist[ny][nx] == d - 1.875) {
										res.from.y = ny, res.from.x = nx;
										get_process_flag = true;
										d -= 1.875;
										break;
									}
								}
								else {
									if (dist[ny][nx] == d - 1.0) {
										res.from.y = ny, res.from.x = nx;
										get_process_flag = true;
										d -= 1.0;
										break;
									}
								}
							}
							if (get_ans_flag)break;
							if (!get_process_flag)
							{
								throw Error{ U"Dijkstraの復元に失敗しました" };
							}
						}
					}
				}
			}
			else {
				int gy = own_move_to[perm[i]].y, gx = own_move_to[perm[i]].x;
				move_dijkstra(inSitua, dist, footprint, ene_pos, own_pos, y, x, gy, gx);
				from = { gx,gy };
				double d = dist[gy][gx];
				if (d != -1.0) {
					while (true) {
						//斜め
						bool get_process_flag = false;
						bool get_ans_flag = false;

						//注目位置が壁でないなら斜めも見る
						if (inSitua.boxes[gy][gx].bld != eneWall) {
							for (int i = 0; i < 4; i++) {
								int ny = gy + cross4[i].y;
								int nx = gx + cross4[i].x;
								if (!OK(ny, nx))continue;
								//currentを見つけた時 距離も確認
								if (ny == y && nx == x && d == 1.0) {
									get_ans_flag = true;
									int dir = 2 * i;
									dir = (dir + 4) % 8 + 1;//方向反転
									act = { 1, dir };
									break;
								}
								//遷移元を発見
								if (dist[ny][nx] == d - 1.0) {
									gy = ny, gx = nx;
									get_process_flag = true;
									d -= 1.0;
									break;
								}
							}
						}
						if (get_process_flag)continue;
						if (get_ans_flag)break;
						//縦横
						for (int i = 0; i < 4; i++) {
							int ny = gy + dxdy4[i].y;
							int nx = gx + dxdy4[i].x;
							if (!OK(ny, nx))continue;
							//currentを見つけた時
							if (ny == y && nx == x) {
								get_ans_flag = true;
								int dir = 2 * i + 1;
								dir = (dir + 4) % 8 + 1;//方向反転
								if (inSitua.boxes[gy][gx].bld == eneWall)
									act = { 3, dir };
								else
									act = { 1, dir };
								break;
							}
							//破壊遷移
							if (inSitua.boxes[gy][gx].bld == eneWall) {
								if (dist[ny][nx] == d - 1.875) {
									gy = ny, gx = nx;
									get_process_flag = true;
									d -= 1.875;
									break;
								}
							}
							else {//通常遷移
								if (dist[ny][nx] == d - 1.0) {
									gy = ny, gx = nx;
									get_process_flag = true;
									d -= 1.0;
									break;
								}
							}
						}
						if (get_ans_flag)break;
						if (!get_process_flag)
						{
							throw Error{ U"Dijkstraの復元に失敗しました" };
						}
					}
				}
			}

			now_hands[perm[i]] = act;
			if (act.hand == 0) {
				now_hands_val -= 50;
			}
			if (act.hand == 1) {
				own_pos[y][x] = false;
				own_pos[y + dxdy8[act.dir - 1].y][x + dxdy8[act.dir - 1].x] = true;
				now_hands_val -= dist[from.y][from.x];
			}
			if (act.hand == 2) {
				inCreate[y + dxdy8[act.dir - 1].y][x + dxdy8[act.dir - 1].x] = false;
				inSitua.boxes[y + dxdy8[act.dir - 1].y][x + dxdy8[act.dir - 1].x].bld = ownWall;
				now_hands_val += 100;
			}
			if (act.hand == 3) {
				inBreak[y + dxdy8[act.dir - 1].y][x + dxdy8[act.dir - 1].x] = false;
				inSitua.boxes[y + dxdy8[act.dir - 1].y][x + dxdy8[act.dir - 1].x].bld = None;
				if (Break[y + dxdy8[act.dir - 1].y][x + dxdy8[act.dir - 1].x])now_hands_val -= dist[from.y][from.x];
				else now_hands_val += 100;
			}
		}

		//手の評価を最大化する
		if (now_hands_val > max_hands_val) {
			max_hands = now_hands;
			max_hands_val = now_hands_val;
		}

	} while (next_permutation(perm.begin(), perm.end()));



	Console << U"hands_val:" << max_hands_val;

	//操作による優先度の変化とjsonの入力を行う
	for (int i = 0; i < current.mason; i++) {
		action act = max_hands[i];
		int y = Situa.owns[i].y, x = Situa.owns[i].x;
		json[U"actions"][i] = JSON{ {U"type",act.hand},{U"dir",act.dir} };
		Console << U"{x,y}:" << Point{y, x} << U" hand:" << act.hand << U" dr:" << act.dir;
		if (act.hand == 1 && y + dxdy8[act.dir - 1].y == own_move_to[i].y && x + dxdy8[act.dir - 1].x == own_move_to[i].x)
			own_move_to[i] = { -1,-1 };
		if (act.hand == 2) {
			Create[y + dxdy8[act.dir - 1].y][x + dxdy8[act.dir - 1].x] = false;
		}
		if (act.hand == 3) {
			Break[y + dxdy8[act.dir - 1].y][x + dxdy8[act.dir - 1].x] = false;
		}
	}
}
